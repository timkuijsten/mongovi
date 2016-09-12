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

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <histedit.h>
#include <libgen.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bson.h>
#include <bcon.h>
#include <mongoc.h>

#include "common.h"
#include "jsonify.h"

#define MAXLINE 1024
#define MAXUSERNAME 100

#define MAXMONGOURL 200

#define MAXPROMPT 30  // must support at least 1 + 4 + 1 + 4 + 3 = 13 characters for the shortened version of a prompt:
                      // "/dbname/collname > " would become "/d..e/c..e > " if MAXPROMPT = 13
#define MAXPROG 10
#define MAXQUERY 16 * 1024

static char *progname;

static char *dbname;
static char *collname;

/* shell specific user info */
typedef struct {
  char name[MAXUSERNAME];
  char home[PATH_MAX];
} user_t;

/* mongo specific db info */
typedef struct {
  char url[MAXMONGOURL];
} config_t;

enum cmd { ILLEGAL = -1, UNKNOWN, LSDBS, CURDB, CHDB, LSCOLLS, CURCOLL, CHCOLL, QUERY, AGQUERY };

typedef struct {
  int tok;
  char *name;
  int nrargs;
} cmd_t;

cmd_t cmds[] = {
  LSDBS,    "dbs",      0,
  CURDB,    "db",       0,
  CHDB,     "db",       1,
  LSCOLLS,  "colls",    0,
  LSCOLLS,  "c",        0,
  CURCOLL,  "coll",     0,
  CHCOLL,   "coll",     1,
  QUERY,    "{",        0,
  AGQUERY,  "[",        0,
};

static user_t user;
static config_t config;

static char p[MAXPROMPT + 1];
char *prompt(EditLine *e);
int init_user(user_t *usr);
int set_prompt(char *dbname, char *collname);
int read_config(user_t *usr, config_t *cfg);
int parse_file(FILE *fp, char *line, config_t *cfg);
int parse_cmd(int argc, const char *argv[]);
int exec_cmd(const int cmd, int argc, const char *argv[], mongoc_client_t *client, mongoc_collection_t *coll, const char *line, int linelen);
int exec_agquery(mongoc_collection_t *collection, const char *line, int len);
int exec_lsdbs(mongoc_client_t *client);
int exec_lscolls(mongoc_client_t *client, char *dbname);

void usage(void)
{
  printf("usage: %s database collection\n", progname);
  exit(0);
}

int main(int argc, char **argv)
{
  const char *line, **mvav;
  int on, read, len, status, i, mvac, mvcc, mvco, cmd;
  EditLine *e;
  History *h;
  HistEvent he;
  Tokenizer *t;

  mongoc_client_t *client;
  mongoc_collection_t *collection;
  bson_error_t error;

  char connect_url[MAXMONGOURL] = "mongodb://localhost:27017";

  progname = basename(argv[0]);

  if (argc != 3)
    usage();

  while (--argc)
    switch (argc) {
    case 1:
      dbname = argv[argc];
      break;
    case 2:
      collname = argv[argc];
      break;
    }

  if (PATH_MAX < 20)
    fatal("can't determine PATH_MAX");

  if (init_user(&user) < 0)
    fatal("can't initialize user");

  if ((status = read_config(&user, &config)) < 0)
    fatal("can't read config file");
  else if (status > 0)
    if (strlcpy(connect_url, config.url, MAXMONGOURL) > MAXMONGOURL)
      fatal("url in config too long");
  // else use default

  set_prompt(dbname, collname);

  if ((h = history_init()) == NULL)
    fatal("can't initialize history");
  if ((e = el_init(argv[0], stdin, stdout, stderr)) == NULL)
    fatal("can't initialize editline");
  t = tok_init(NULL);

  el_set(e, EL_HIST, history, h);
  el_set(e, EL_PROMPT, prompt);
  el_get(e, EL_EDITMODE, &on);
  el_source(e, NULL);

  history(h, &he, H_SETSIZE, 100);

  // setup mongo
  mongoc_init();
  if ((client = mongoc_client_new(connect_url)) == NULL)
    fatal("can't connect to mongo");

  collection = mongoc_client_get_collection(client, dbname, collname);

  while ((line = el_gets(e, &read)) != NULL) {
    // tokenize
    tok_reset(t);
    tok_line(t, el_line(e), &mvac, &mvav, &mvcc, &mvco);

    if (mvac == 0)
      continue;

    cmd = parse_cmd(mvac, mvav);
    switch (cmd) {
    case UNKNOWN:
    case ILLEGAL:
      fprintf(stderr, "illegal syntax\n");
      continue;
      break;
    }

    if (history(h, &he, H_ENTER, line) == -1)
      fatal("can't enter history");

    if (exec_cmd(cmd, mvac, mvav, client, collection, line, strlen(line) - 1) == -1) {
      fprintf(stderr, "execution failed\n");
    }
  }

  if (read == -1)
    fstrerror();

  mongoc_collection_destroy(collection);
  mongoc_client_destroy(client);
  mongoc_cleanup();

  tok_end(t);
  history_end(h);
  el_end(e);

  printf("\n");
  return 0;
}

// return command code
int parse_cmd(int argc, const char *argv[])
{
  int i;
  for (i = 0; i < argc; i++)
    if (strcmp("dbs", argv[i]) == 0) {
      return LSDBS;
    } else if (strcmp("db", argv[i]) == 0) {
      switch (argc) {
      case 0:
        return CURDB;
      case 1:
        return CHDB;
      default:
        return ILLEGAL;
      }
    } else if (strcmp("colls", argv[i]) == 0) {
      return LSCOLLS;
    } else if (strcmp("c", argv[i]) == 0) {
      return LSCOLLS;
    } else if (strcmp("coll", argv[i]) == 0) {
      if (argc > 1)
        return CHCOLL;
      else
        return CURCOLL;
    } else if (argv[0][0] == '{') {
      return QUERY;
    } else if (argv[0][0] == '[') {
      return AGQUERY;
    }

  return -1;
}

// execute command with given arguments
// return 0 on success, -1 on failure
int exec_cmd(const int cmd, int argc, const char *argv[], mongoc_client_t *client, mongoc_collection_t *coll, const char *line, int linelen)
{
  int i;

  switch (cmd) {
  case LSDBS:
    return exec_lsdbs(client);
  case CURDB:
    break;
  case CHDB:
    break;
  case ILLEGAL:
    break;
  case LSCOLLS:
    return exec_lscolls(client, dbname);
  case CHCOLL:
    break;
  case CURCOLL:
    break;
  case QUERY:
    break;
  case AGQUERY:
    return exec_agquery(coll, line, linelen);
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

// execute an aggregation pipeline
// return 0 on success, -1 on failure
int exec_agquery(mongoc_collection_t *collection, const char *line, int len)
{
  mongoc_cursor_t *cursor;
  bson_error_t error;
  const bson_t *doc;
  char *str;
  bson_t aggr_query;
  char query_doc[MAXQUERY];

  // try to parse as loose json and convert to strict json
  if (relaxed_to_strict(query_doc, MAXQUERY, line, len) == -1)
    fatal("jsonify error");

  // try to parse it as json and convert to bson
  if (!bson_init_from_json(&aggr_query, query_doc, -1, &error)) {
    fprintf(stderr, "%s\n", error.message);
    return -1;
  }

  cursor = mongoc_collection_aggregate(collection, MONGOC_QUERY_NONE, &aggr_query, NULL, NULL);

  while (mongoc_cursor_next(cursor, &doc)) {
    str = bson_as_json(doc, NULL);
    printf ("%s\n", str);
    bson_free(str);
  }

  if (mongoc_cursor_error(cursor, &error)) {
    fprintf(stderr, "cursor failed: %s\n", error.message);
    mongoc_cursor_destroy(cursor);
    return -1;
  }

  mongoc_cursor_destroy(cursor);

  return 0;
}

char *prompt(EditLine *e)
{
  return p;
}

// if too long, shorten first or both components
// global p should have space for MAXPROMPT + 1 bytes
int
set_prompt(char *dbname, char *collname)
{
  const int static_chars = 5; // prompt is of the form "/d/c > "
  char c1[MAXPROMPT + 1], c2[MAXPROMPT + 1];
  int plen, i;

  strlcpy(c1, dbname, MAXPROMPT);
  strlcpy(c2, collname, MAXPROMPT);

  plen = static_chars + strlen(c1) + strlen(c2);

  // ensure prompt fits
  if (plen - MAXPROMPT > 0)
    if (shorten_comps(c1, c2, MAXPROMPT - static_chars) < 0)
      fatal("can't initialize prompt");

  snprintf(p, MAXPROMPT + 1, "/%s/%s > ", c1, c2);
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
    if (errno != ENOENT) {
      printf("errno %d\n", errno);
      ferrno(strerror(errno));
      return -1;
    } else {
      return 0;
    }
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
