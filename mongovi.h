#ifndef MONGOVI_H
#define MONGOVI_H

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

#define _POSIX_C_SOURCE  200809L

#include "jsonify.h"
#include "shorten.h"
#include "prefix_match.h"

#include <bson.h>
#include <bcon.h>
#include <mongoc.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <histedit.h>
#include <libgen.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#define MAXLINE 16 * 1024
#define MAXUSERNAME 100

#define MAXMONGOURL 200
#define MAXDBNAME 200
#define MAXCOLLNAME 200

#define MAXPROMPT 30  /* must support at least 1 + 4 + 1 + 4 + 2 = 12 characters
                         for the minimally shortened version of a prompt.
                         if MAXPROMPT = 12 then "/dbname/collname> " would
                         become "/d..e/c..e> " */
#define MAXPROG 10
#define MAXDOC 16 * 100 * 1024      /* maximum size of a json document */

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

/* shell specific user info */
typedef struct {
  char name[MAXUSERNAME];
  char home[PATH_MAX];
} user_t;

typedef struct {
  char dbname[MAXDBNAME];
  char collname[MAXCOLLNAME];
} path_t;

/* mongo specific db info */
typedef struct {
  char url[MAXMONGOURL];
} config_t;

enum cmd { ILLEGAL = -1, UNKNOWN, AMBIGUOUS, DROP, LS, CHCOLL, COUNT, UPDATE, UPSERT, INSERT, REMOVE, FIND, AGQUERY, HELP };
enum errors { DBMISSING = 256, COLLMISSING };

void usage(void);
int main_init(int argc, char **argv);
char *prompt();
unsigned char complete(EditLine *e, int ch);
int complete_cmd(EditLine *e, const char *tok, int co);
int complete_path(EditLine *e, const char *tok, int co);
int init_user(user_t *usr);
int set_prompt(const char *dbname, const char *collname);
int read_config(user_t *usr, config_t *cfg);
int idtosel(char *doc, const size_t docsize, const char *sel, const size_t sellen);
long parse_selector(char *doc, size_t docsize, const char *line, int len);
int parse_path(const char *paths, path_t *newpath, int *dbstart, int *collstart);
int mv_parse_file(FILE *fp, config_t *cfg);
int mv_parse_cmd(int argc, const char *argv[], const char *line, char **lp);
int exec_cmd(const int cmd, const char **argv, const char *line, int linelen);
int exec_drop(const char *npath);
int exec_ls(const char *npath);
int exec_lsdbs(mongoc_client_t *client, const char *prefix);
int exec_lscolls(mongoc_client_t *client, char *dbname);
int exec_chcoll(mongoc_client_t *client, const path_t newpath);
int exec_count(mongoc_collection_t *collection, const char *line, int len);
int exec_update(mongoc_collection_t *collection, const char *line, int upsert);
int exec_insert(mongoc_collection_t *collection, const char *line, int len);
int exec_remove(mongoc_collection_t *collection, const char *line, int len);
int exec_query(mongoc_collection_t *collection, const char *line, int len, int idsonly);
int exec_agquery(mongoc_collection_t *collection, const char *line, int len);

#endif
