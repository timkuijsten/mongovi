/**
 * Copyright (c) 2016 Tim Kuijsten
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "jsonify.h"
#include "shorten.h"

#include <bson.h>
#include <bcon.h>
#include <mongoc.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <histedit.h>
#include <libgen.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXLINE 1024
#define MAXUSERNAME 100

#define MAXMONGOURL 200
#define MAXDBNAME 200
#define MAXCOLLNAME 200

#define MAXPROMPT 30  /* must support at least 1 + 4 + 1 + 4 + 2 = 12 characters
                         for the minimally shortened version of a prompt.
                         if MAXPROMPT = 12 then "/dbname/collname> " would
                         become "/d..e/c..e> " */
#define MAXPROG 10
#define MAXDOC 16 * 1024      /* maximum size of a json document */

static char progname[MAXPROG];

static char dbname[MAXDBNAME];
static char collname[MAXCOLLNAME];

/* shell specific user info */
typedef struct {
  char name[MAXUSERNAME];
  char home[PATH_MAX];
} user_t;

/* mongo specific db info */
typedef struct {
  char url[MAXMONGOURL];
} config_t;

enum cmd { ILLEGAL = -1, UNKNOWN, LSDBS, LSCOLLS, CHCOLL, COUNT, UPDATE, INSERT, REMOVE, FIND, AGQUERY };

typedef struct {
  int tok;
  char *name;
  int nrargs;
} cmd_t;

cmd_t cmds[] = {
  { LSDBS,    "dbs",      0 },
  { LSCOLLS,  "c",        0 }, // list all collections
  { LSCOLLS,  "colls",    0 }, // alias for c
  { CHCOLL,   "c",        1 }, // with an argument of the new database and/or collection
  { COUNT,    "count",    1 },
  { UPDATE,   "update",   2 },
  { INSERT,   "insert",   1 },
  { REMOVE,   "remove",   1 },
  { FIND,     "find",     1 },
  { FIND,     "{",        0 }, // shortcut for find
  { AGQUERY,  "[",        0 },
};

static user_t user;
static config_t config;

static char p[MAXPROMPT + 1];
char *prompt();
int init_user(user_t *usr);
int set_prompt(char *dbname, char *collname);
int read_config(user_t *usr, config_t *cfg);
int parse_file(FILE *fp, char *line, config_t *cfg);
int parse_cmd(int argc, const char *argv[], const char *line, char **lp);
int exec_cmd(const int cmd, const char **argv, const char *line, int linelen);
int exec_lsdbs(mongoc_client_t *client);
int exec_lscolls(mongoc_client_t *client, char *dbname);
int exec_chcoll(mongoc_client_t *client, const char *newname);
int exec_count(mongoc_collection_t *collection, const char *line, int len);
int exec_update(mongoc_collection_t *collection, const char *line);
int exec_insert(mongoc_collection_t *collection, const char *line, int len);
int exec_remove(mongoc_collection_t *collection, const char *line, int len);
int exec_query(mongoc_collection_t *collection, const char *line, int len);
int exec_agquery(mongoc_collection_t *collection, const char *line, int len);

static mongoc_client_t *client;
static mongoc_collection_t *ccoll; // current collection

void usage(void)
{
  printf("usage: %s database collection\n", progname);
  exit(0);
}

int main(int argc, char **argv)
{
  const char *line, **av;
  char linecpy[MAXLINE], *lp;
  int read, status, ac, cc, co, cmd;
  EditLine *e;
  History *h;
  HistEvent he;
  Tokenizer *t;

  char connect_url[MAXMONGOURL] = "mongodb://localhost:27017";

  if (strlcpy(progname, basename(argv[0]), MAXPROG) > MAXPROG)
    errx(1, "program name too long");

  if (argc != 3)
    usage();

  while (--argc)
    switch (argc) {
    case 1:
      if (strlcpy(dbname, argv[argc], MAXDBNAME) > MAXDBNAME)
        errx(1, "can't set database name");
      break;
    case 2:
      if (strlcpy(collname, argv[argc], MAXCOLLNAME) > MAXCOLLNAME)
        errx(1, "can't set collection name");
      break;
    }

  if (PATH_MAX < 20)
    errx(1, "can't determine PATH_MAX");

  if (init_user(&user) < 0)
    errx(1, "can't initialize user");

  if ((status = read_config(&user, &config)) < 0)
    errx(1, "can't read config file");
  else if (status > 0)
    if (strlcpy(connect_url, config.url, MAXMONGOURL) > MAXMONGOURL)
      errx(1, "url in config too long");
  // else use default

  set_prompt(dbname, collname);

  if ((e = el_init(progname, stdin, stdout, stderr)) == NULL)
    errx(1, "can't initialize editline");
  if ((h = history_init()) == NULL)
    errx(1, "can't initialize history");
  t = tok_init(NULL);

  history(h, &he, H_SETSIZE, 100);
  el_set(e, EL_HIST, history, h);

  el_set(e, EL_PROMPT, prompt);
  el_set(e, EL_EDITOR, "emacs");
  el_set(e, EL_TERMINAL, NULL);
  el_source(e, NULL);

  // setup mongo
  mongoc_init();
  if ((client = mongoc_client_new(connect_url)) == NULL)
    errx(1, "can't connect to mongo");

  ccoll = mongoc_client_get_collection(client, dbname, collname);

  while ((line = el_gets(e, &read)) != NULL) {
    if (read > MAXLINE)
      errx(1, "line too long");

    if (line[read - 1] != '\n')
      errx(1, "expected line to end with a newline");

    // tokenize
    tok_reset(t);
    tok_line(t, el_line(e), &ac, &av, &cc, &co);

    if (ac == 0)
      continue;

    // copy without newline
    if (strlcpy(linecpy, line, read) > (size_t)read)
      errx(1, "could not copy line");

    if (history(h, &he, H_ENTER, linecpy) == -1)
      errx(1, "can't enter history");

    cmd = parse_cmd(ac, av, linecpy, &lp);
    switch (cmd) {
    case UNKNOWN:
      warnx("unknown command");
      continue;
      break;
    case ILLEGAL:
      warnx("illegal syntax");
      continue;
      break;
    }

    if (exec_cmd(cmd, av, lp, strlen(lp)) == -1) {
      warnx("execution failed");
    }
  }

  if (read == -1)
    err(1, "");

  mongoc_collection_destroy(ccoll);
  mongoc_client_destroy(client);
  mongoc_cleanup();

  tok_end(t);
  history_end(h);
  el_end(e);

  if (isatty(STDIN_FILENO))
    printf("\n");

  return 0;
}

// return command code
int parse_cmd(int argc, const char *argv[], const char *line, char **lp)
{
  // expect command to be the first token
  int i = 0;
  if (strcmp("dbs", argv[i]) == 0) {
    *lp = strstr(line, "dbs") + strlen("dbs");
    switch (argc) {
    case 1:
      return LSDBS;
    default:
      return ILLEGAL;
    }
  } else if (strcmp("colls", argv[i]) == 0) {
    *lp = strstr(line, "colls") + strlen("colls");
    switch (argc) {
    case 1:
      return LSCOLLS;
    default:
      return ILLEGAL;
    }
  } else if (strcmp("c", argv[i]) == 0) {
    *lp = strstr(line, "c") + strlen("c");
    switch (argc) {
    case 1:
      return LSCOLLS;
    case 2:
      return CHCOLL;
    default:
      return ILLEGAL;
    }
  } else if (strcmp("count", argv[i]) == 0) {
    *lp = strstr(line, "count") + strlen("count");
    return COUNT;
  } else if (strcmp("update", argv[i]) == 0) {
    *lp = strstr(line, "update") + strlen("update");
    return UPDATE;
  } else if (strcmp("insert", argv[i]) == 0) {
    *lp = strstr(line, "insert") + strlen("insert");
    return INSERT;
  } else if (strcmp("remove", argv[i]) == 0) {
    *lp = strstr(line, "remove") + strlen("remove");
    return REMOVE;
  } else if (strcmp("find", argv[i]) == 0) {
    *lp = strstr(line, "find") + strlen("find");
    return FIND;
  } else if (argv[0][0] == '{') {
    *lp = (char *)line;
    return FIND;
  } else if (argv[0][0] == '[') {
    *lp = (char *)line;
    return AGQUERY;
  }

  return UNKNOWN;
}

// execute command with given arguments
// return 0 on success, -1 on failure
int exec_cmd(const int cmd, const char **argv, const char *line, int linelen)
{
  switch (cmd) {
  case LSDBS:
    return exec_lsdbs(client);
  case ILLEGAL:
    break;
  case LSCOLLS:
    return exec_lscolls(client, dbname);
  case CHCOLL:
    return exec_chcoll(client, argv[1]);
  case COUNT:
    return exec_count(ccoll, line, linelen);
  case UPDATE:
    return exec_update(ccoll, line);
  case INSERT:
    return exec_insert(ccoll, line, linelen);
  case REMOVE:
    return exec_remove(ccoll, line, linelen);
  case FIND:
    return exec_query(ccoll, line, linelen);
  case AGQUERY:
    return exec_agquery(ccoll, line, linelen);
  }

  return -1;
}

// list database for the given client
// return 0 on success, -1 on failure
int exec_lsdbs(mongoc_client_t *client)
{
  bson_error_t error;
  char **strv;
  int i;

  if ((strv = mongoc_client_get_database_names(client, &error)) == NULL)
    return -1;

  for (i = 0; strv[i]; i++)
    printf("%s\n", strv[i]);

  bson_strfreev(strv);

  return 0;
}

// list collections for the given database
// return 0 on success, -1 on failure
int exec_lscolls(mongoc_client_t *client, char *dbname)
{
  bson_error_t error;
  mongoc_database_t *db;
  char **strv;
  int i;

  db = mongoc_client_get_database(client, dbname);

  if ((strv = mongoc_database_get_collection_names(db, &error)) == NULL)
    return -1;

  for (i = 0; strv[i]; i++)
    printf("%s\n", strv[i]);

  bson_strfreev(strv);
  mongoc_database_destroy(db);

  return 0;
}

// change collection and optionally the database
// return 0 on success, -1 on failure
int exec_chcoll(mongoc_client_t *client, const char *newname)
{
  int i, ac;
  const char **av, *collp;
  char newdb[MAXDBNAME];
  char newcoll[MAXCOLLNAME];
  Tokenizer *t;

  // default to current db
  strlcpy(newdb, dbname, MAXDBNAME);

  // assume newname has no database component
  collp = newname;

  // check if there is a database component
  if (newname[0] == '/') {
    t = tok_init("/");
    tok_str(t, newname, &ac, &av);
    if (ac > 1) { // use first component as the name of the database
      if ((i = strlcpy(newdb, av[0], MAXDBNAME)) > MAXDBNAME)
        return -1;
      collp += 1 + i + 1; // skip db name and it's leading and trailing slash
    }
    tok_end(t);
  }
  if (strlcpy(newcoll, collp, MAXCOLLNAME) > MAXCOLLNAME)
    return -1;

  mongoc_collection_destroy(ccoll);
  ccoll = mongoc_client_get_collection(client, newdb, newcoll);

  // update references
  strlcpy(dbname, newdb, MAXDBNAME);
  strlcpy(collname, newcoll, MAXCOLLNAME);

  set_prompt(newdb, newcoll);

  return 0;
}

// count number of documents in collection
// return 0 on success, -1 on failure
int exec_count(mongoc_collection_t *collection, const char *line, int len)
{
  bson_error_t error;
  int64_t count;
  bson_t query;
  char query_doc[MAXDOC] = "{}"; /* default to all documents */

  // try to parse as relaxed json and convert to strict json
  if (len)
    if (relaxed_to_strict(query_doc, MAXDOC, line, len, 0) == -1)
      errx(1, "jsonify error");

  // try to parse it as json and convert to bson
  if (!bson_init_from_json(&query, query_doc, -1, &error)) {
    warnx("%s", error.message);
    return -1;
  }

  if ((count = mongoc_collection_count(collection, MONGOC_QUERY_NONE, &query, 0, 0, NULL, &error)) == -1)
    warnx("cursor failed: %s", error.message);

  printf("%lld\n", count);

  return 0;
}

// parse update command, expect two json objects, a selector, and an update doc and exec
int exec_update(mongoc_collection_t *collection, const char *line)
{
  long offset;
  char query_doc[MAXDOC];
  char update_doc[MAXDOC];
  bson_error_t error;
  bson_t query, update;

  // read first json object
  if ((offset = relaxed_to_strict(query_doc, MAXDOC, line, strlen(line), 1)) == -1)
    return ILLEGAL;
  if (offset == 0)
    return ILLEGAL;

  // shorten line
  line += offset;

  // read second json object
  if ((offset = relaxed_to_strict(update_doc, MAXDOC, line, strlen(line), 1)) == -1)
    return ILLEGAL;
  if (offset == 0)
    return ILLEGAL;

  // shorten line
  line += offset;

  // try to parse the query as json and convert to bson
  if (!bson_init_from_json(&query, query_doc, -1, &error)) {
    warnx("%s", error.message);
    return -1;
  }

  // try to parse the update as json and convert to bson
  if (!bson_init_from_json(&update, update_doc, -1, &error)) {
    warnx("%s", error.message);
    return -1;
  }

  // execute update
  if (!mongoc_collection_update(collection, MONGOC_UPDATE_MULTI_UPDATE, &query, &update, NULL, &error)) {
    warnx("%s", error.message);
    return -1;
  }

  return 0;
}

// parse insert command, expect one json objects, the insert doc and exec
int exec_insert(mongoc_collection_t *collection, const char *line, int len)
{
  long offset;
  char insert_doc[MAXDOC];
  bson_error_t error;
  bson_t doc;

  // read first json object
  if ((offset = relaxed_to_strict(insert_doc, MAXDOC, line, len, 1)) == -1)
    return ILLEGAL;
  if (offset == 0)
    return ILLEGAL;

  // try to parse the doc as json and convert to bson
  if (!bson_init_from_json(&doc, insert_doc, -1, &error)) {
    warnx("%s", error.message);
    return -1;
  }

  // execute insert
  if (!mongoc_collection_insert(collection, MONGOC_INSERT_NONE, &doc, NULL, &error)) {
    warnx("%s", error.message);
    return -1;
  }

  return 0;
}

// parse remove command, expect one selector
int exec_remove(mongoc_collection_t *collection, const char *line, int len)
{
  long offset;
  char remove_doc[MAXDOC];
  bson_error_t error;
  bson_t doc;

  // read first json object
  if ((offset = relaxed_to_strict(remove_doc, MAXDOC, line, len, 1)) == -1)
    return ILLEGAL;
  if (offset == 0)
    return ILLEGAL;

  // try to parse the doc as json and convert to bson
  if (!bson_init_from_json(&doc, remove_doc, -1, &error)) {
    warnx("%s", error.message);
    return -1;
  }

  // execute remove
  if (!mongoc_collection_remove(collection, MONGOC_REMOVE_NONE, &doc, NULL, &error)) {
    warnx("%s", error.message);
    return -1;
  }

  return 0;
}

// execute a query
// return 0 on success, -1 on failure
int exec_query(mongoc_collection_t *collection, const char *line, int len)
{
  mongoc_cursor_t *cursor;
  bson_error_t error;
  const bson_t *doc;
  char *str;
  bson_t query;
  char query_doc[MAXDOC] = "{}"; /* default to all documents */

  // try to parse as relaxed json and convert to strict json
  if (len)
    if (relaxed_to_strict(query_doc, MAXDOC, line, len, 0) == -1)
      errx(1, "jsonify error");

  // try to parse it as json and convert to bson
  if (!bson_init_from_json(&query, query_doc, -1, &error)) {
    warnx("%s", error.message);
    return -1;
  }

  cursor = mongoc_collection_find(collection, MONGOC_QUERY_NONE, 0, 0, 0, &query, NULL, NULL);

  while (mongoc_cursor_next(cursor, &doc)) {
    str = bson_as_json(doc, NULL);
    printf ("%s\n", str);
    bson_free(str);
  }

  if (mongoc_cursor_error(cursor, &error)) {
    warnx("cursor failed: %s", error.message);
    mongoc_cursor_destroy(cursor);
    return -1;
  }

  mongoc_cursor_destroy(cursor);

  return 0;
}

// execute an aggregation pipeline
// return 0 on success, -1 on failure
int exec_agquery(mongoc_collection_t *collection, const char *line, int len)
{
  mongoc_cursor_t *cursor;
  bson_error_t error;
  const bson_t *doc;
  char *str;
  bson_t aggr_query;
  char query_doc[MAXDOC];

  // try to parse as relaxed json and convert to strict json
  if (relaxed_to_strict(query_doc, MAXDOC, line, len, 0) == -1)
    errx(1, "jsonify error");

  // try to parse it as json and convert to bson
  if (!bson_init_from_json(&aggr_query, query_doc, -1, &error)) {
    warnx("%s", error.message);
    return -1;
  }

  cursor = mongoc_collection_aggregate(collection, MONGOC_QUERY_NONE, &aggr_query, NULL, NULL);

  while (mongoc_cursor_next(cursor, &doc)) {
    str = bson_as_json(doc, NULL);
    printf ("%s\n", str);
    bson_free(str);
  }

  if (mongoc_cursor_error(cursor, &error)) {
    warnx("cursor failed: %s", error.message);
    mongoc_cursor_destroy(cursor);
    return -1;
  }

  mongoc_cursor_destroy(cursor);

  return 0;
}

char *prompt()
{
  return p;
}

// if too long, shorten first or both components
// global p should have space for MAXPROMPT + 1 bytes
int
set_prompt(char *dbname, char *collname)
{
  const int static_chars = 4; /* prompt is of the form "/d/c> " */
  char c1[MAXPROMPT + 1], c2[MAXPROMPT + 1];
  int plen;

  strlcpy(c1, dbname, MAXPROMPT);
  strlcpy(c2, collname, MAXPROMPT);

  plen = static_chars + strlen(c1) + strlen(c2);

  // ensure prompt fits
  if (plen - MAXPROMPT > 0)
    if (shorten_comps(c1, c2, MAXPROMPT - static_chars) < 0)
      errx(1, "can't initialize prompt");

  snprintf(p, MAXPROMPT + 1, "/%s/%s> ", c1, c2);
  return 0;
}

// set username and home dir
// return 0 on success or -1 on failure.
int
init_user(user_t *usr)
{
  struct passwd *pw;

  if ((pw = getpwuid(getuid())) == NULL)
    return -1; // user not found
  if (strlcpy(usr->name, pw->pw_name, MAXUSERNAME) >= MAXUSERNAME)
    return -1; // username truncated
  if (strlcpy(usr->home, pw->pw_dir, PATH_MAX) >= PATH_MAX)
    return -1; // home dir truncated

  return 0;
}

// try to read ~/.mongovi and set cfg
// return 1 if config is read and set, 0 if no config is found or -1 on failure.
int
read_config(user_t *usr, config_t *cfg)
{
  const char *file = ".mongovi";
  char tmppath[PATH_MAX + 1], *line;
  FILE *fp;

  line = NULL;

  if (strlcpy(tmppath, usr->home, PATH_MAX) >= PATH_MAX)
    return -1;
  if (strlcat(tmppath, "/", PATH_MAX) >= PATH_MAX)
    return -1;
  if (strlcat(tmppath, file, PATH_MAX) >= PATH_MAX)
    return -1;

  if ((fp = fopen(tmppath, "re")) == NULL) {
    if (errno == ENOENT)
      return 0;

    return -1;
  }

  if (parse_file(fp, line, cfg) < 0) {
    if (line != NULL)
      free(line);
    fclose(fp);
    return -1;
  }

  free(line);
  fclose(fp);
  return 1;
}

// read the credentials from a users config file
// return 0 on success or -1 on failure.
int
parse_file(FILE *fp, char *line, config_t *cfg)
{
  size_t linesize = 0;
  ssize_t linelen = 0;

  // expect url on first line
  if ((linelen = getline(&line, &linesize, fp)) < 0)
    return -1;
  if (linelen > MAXMONGOURL)
    return -1;
  if (strlcpy(cfg->url, line, MAXMONGOURL) >= MAXMONGOURL)
    return -1;
  cfg->url[linelen - 1] = '\0'; // trim newline

  return 0;
}
