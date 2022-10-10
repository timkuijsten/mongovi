/*
 * Copyright (c) 2016, 2022 Tim Kuijsten
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

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <locale.h>
#include <assert.h>
#include <err.h>
#include <histedit.h>
#include <libgen.h>
#include <pwd.h>

#include <bson.h>
#include <mongoc.h>

#include "compat/compat.h"
#include "jsonify.h"
#include "shorten.h"
#include "prefix_match.h"
#include "parse_path.h"

#ifndef VERSION_MAJOR
#define VERSION_MAJOR 0
#define VERSION_MINOR 0
#define VERSION_PATCH 0
#endif

#define MAXLINE 16 * 100 * 1024
#define MAXUSERNAME 100

#define DFLMONGOURL "mongodb://localhost:27017"
#define MAXMONGOURL 200

#define DOTFILE ".mongovi"

#define MAXPROG 10
#define MAXDOC 16 * 100 * 1024	/* maximum size of a json document */

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define BULKINSERTMAX 10000

#define MAXPROMPTCOLUMNS 30	/* The maximum number of columns the prompt may
				   use. Should be at least "/x..y/x..y> " = 4 +
				   4 + 2 * 4 = 16 since x and y can take at
				   most two columns for one character.
				   NOTE: some UTF-8 characters consume 0 or 2
				   columns. */

static char progname[MAXPROG];

static path_t path, prevpath;

/* use as temporary one-time storage while building a query or query results */
static uint8_t tmpdocs[16 * 1024 * 1024];

/*
 * Make sure the prompt can hold MAXPROMPTCOLUMNS + a trailing null. Since
 * MB_CUR_MAX is only defined after setlocale() is executed, we assume it is
 * less than 8 bytes.
 */
static char pmpt[8 * MAXPROMPTCOLUMNS + 8] = "/> ";

static mongoc_client_t *client;
static mongoc_collection_t *ccoll;	/* current collection */

/* print human readable or not */
static int hr;
static int ttyin, ttyout;

static int import = 0;

static const char *cmds[] = {
	"aggregate",
	"cd",
	"count",
	"drop",
	"find",
	"help",
	"insert",
	"ls",
	"remove",
	"update",
	"upsert",
	NULL
};

/*
 * Given a word, a cursor position in the word and a list of options, complete
 * the word to the longest common prefix and print a list of remaining options,
 * if any.
 *
 * Returns 1 if word is now complete to one option in opts (possibly by prefix
 * extension) or 0 if not.
 *
 * If one option is selected and *selected is not NULL, it is updated to point
 * into **opts to the selected option.
 *
 * 0. if no option has the prefix of word or the prefix matches more than one
 *    option
 * 1. if word matches one option
 */
static int
complete_word(EditLine *e, const char *word, size_t wordlen, const char **opts,
    const char **selected)
{
	const char **matches;
	char *prefix;
	size_t lcplen;
	int r, i;

	prefix = strndup(word, wordlen);
	if (prefix == NULL) {
		warn("complete_word strndup");
		abort();
	}

	if (prefix_match(&matches, opts, prefix) == -1) {
		warn("complete_word prefix_match");
		abort();
	}
	free(prefix);

	if (matches[0] == NULL) {
		free(matches);
		return 0;
	}

	if (matches[1] == NULL) {
		lcplen = strlen(matches[0]);
	} else {
		printf("\n");
		for (i = 0; matches[i] != NULL; i++)
			printf("%s\n", matches[i]);

		lcplen = common_prefix(matches);
	}

	if (lcplen > wordlen) {
		/* zip up */
		prefix = strndup(&matches[0][wordlen], lcplen - wordlen);
		if (prefix == NULL) {
			warn("complete_word strndup matches[0]");
			abort();
		}
		el_insertstr(e, prefix);
		free(prefix);

		wordlen = lcplen;
	}

	r = 0;
	if (matches[1] == NULL) {
		/* word is complete */
		if (selected != NULL)
			*selected = matches[0];

		r = 1;
	}

	free(matches);
	return r;
}

/*
 * Complete a /database/collection path.
 *
 * If more than one option matches the same prefix, all options are printed and
 * the common prefix is completed. If exactly one component matches it is
 * completed.
 *
 * Return 0 on success or -1 on failure.
 */
static int
complete_path(EditLine *e, const char *npath, size_t npathlen)
{
	path_t tmppath;
	bson_error_t error;
	mongoc_database_t *db;
	char p[PATH_MAX], p2[PATH_MAX], lastchar;
	int i, comps;
	char **strv;
	size_t n;

	if (npathlen >= sizeof(p2)) {
		warnx("complete_path p2 too small: %s", npath);
		return -1;
	}

	strncpy(p2, npath, npathlen);
	p2[npathlen] = '\0';

	if (npathlen > 0) {
		lastchar = p2[npathlen - 1];
	} else {
		lastchar = '\0';
	}

	/* copy current context */
	if ((size_t)snprintf(p, sizeof(p), "/%s/%s", path.dbname, path.collname)
	    >= sizeof(p)) {
		warnx("complete_path p too small: %s", p2);
		return -1;
	}

	n = resolvepath(p, sizeof(p), p2, &comps);
	if (n == (size_t)-1) {
		warnx("complete_path resolvepath error: %s", p2);
		return -1;
	}

	if (parse_path(&tmppath, p) == -1) {
		warnx("parse_path error: %s", p2);
		abort();
	}

	if (comps > 1 || (comps == 1 && lastchar == '/') || (comps == 1 && lastchar == '\0')) {
		db = mongoc_client_get_database(client, tmppath.dbname);
		strv = mongoc_database_get_collection_names_with_opts(db, NULL,
		    &error);
		mongoc_database_destroy(db);
		db = NULL;
		if (strv == NULL) {
			warnx("cd coll failed %d.%d %s", error.domain,
			    error.code, error.message);
			return -1;
		}

		i = complete_word(e, tmppath.collname, strlen(tmppath.collname),
		    (const char **)strv, NULL);
		bson_strfreev(strv);
		strv = NULL;

		if (i == 1 && lastchar != ' ' && lastchar != '\0')
			el_insertstr(e, " ");
	} else {
		strv = mongoc_client_get_database_names_with_opts(client, NULL,
		    &error);
		if (strv == NULL) {
			warnx("cd db failed %d.%d %s", error.domain, error.code,
			    error.message);
			return -1;
		}

		i = complete_word(e, tmppath.dbname, strlen(tmppath.dbname),
		    (const char **)strv, NULL);
		bson_strfreev(strv);
		strv = NULL;

		/* append trailing "/" if word is completed or relative root */
		if ((i == 1 && lastchar != '/') || (comps == 0 && lastchar != '/'))
			el_insertstr(e, "/");
	}

	return 0;
}

/*
 * Tab complete commands and path arguments.
 *
 * if empty, print all commands
 * if matches more than one command, print all with matching prefix
 * if matches exactly one command and not complete, complete
 * if command is complete and needs args, look at that
 */
static uint8_t
complete(EditLine *e, __attribute__((unused)) int ch)
{
	Tokenizer *t;
	const char **av;
	int i, rc, ac, cc, co;

	rc = CC_ERROR;

	t = tok_init(NULL);
	if (tok_line(t, el_line(e), &ac, &av, &cc, &co) != 0)
		return rc;

	if (ac == 0) {
		i = 0;
		printf("\n");
		while (cmds[i] != NULL)
			printf("%s\n", cmds[i++]);
		rc = CC_REDISPLAY;
		goto cleanup;
	}

	/*
	 * If the first word is currently selected, try to complete as a
	 * command, if the second word is selected and the command (first word)
	 * supports a path argument, try to complete as a path.
	 */
	if (cc == 0) {
		/* append trailing " " if command is complete(d) */
		i = complete_word(e, av[cc], co, cmds, NULL);
		if (i == 1)
			el_insertstr(e, " ");

		rc = CC_REDISPLAY;
	} else if (cc == 1) {
		if (strcmp(av[0], "cd") == 0 || strcmp(av[0], "ls") == 0 ||
		    strcmp(av[0], "drop") == 0) {
			if (complete_path(e, av[cc], co) == -1) {
				warnx("complete_path error");
				goto cleanup;
			}
		}
		rc = CC_REDISPLAY;
	} else {
		/* ignore subsequent words */
		rc = CC_NORM;
	}

cleanup:
	tok_end(t);

	return rc;
}

/*
 * Update **line and return size of the token pointed to. The separator is
 * always an ASCII SPACE (0x20).
 */
static size_t
nexttok(const char **line)
{
	*line = &(*line)[strspn(*line, " ")];
	return strcspn(*line, " ");
}

/*
 * Create a mongo extended JSON id selector document. If selector is 24 hex
 * digits treat it as an object id, otherwise as a literal.
 *
 * dst     - resulting json doc is written to dst
 * dstsize - the size of dst
 * sel     - selector, must be NUL terminated
 * sellen  - length of sel, excluding the terminating NUL character
 *
 * Return 0 on success, -1 on failure.
 */
static int
idtosel(char *dst, size_t dstsize, const char *sel, size_t sellen)
{
	const size_t oidlen = 24;
	size_t n;

	if (dstsize < 1 || sellen < 1)
		return -1;

	/* if 24 hex chars, assume an object id otherwise treat as a literal */
	if (sellen == oidlen && (strspn(sel, "0123456789abcdefABCDEF") ==
	    oidlen)) {
		n = snprintf(dst, dstsize,
		    "{ \"_id\": { \"$oid\": \"%.*s\" } }", (int)oidlen, sel);
		if (n >= dstsize)
			return -1;
	} else {
		n = snprintf(dst, dstsize, "{ \"_id\": \"%.*s\" }", (int)sellen,
		    sel);
		if (n >= dstsize)
			return -1;
	}

	return 0;
}

/*
 * Parse the selector in line, that is the first (relaxed) json object or
 * literal id. A strictly conforming JSON object is written in "doc" on
 * success.
 *
 * "line" must be null terminated and "linelen" must exclude the terminating
 * null byte.
 *
 * Return the number of bytes parsed on success or -1 on failure.
 * On success, if docsize > 0, a null byte is always written to doc.
 */
static int
parse_selector(uint8_t *doc, size_t docsize, const char *line, size_t linelen)
{
	const char *id;
	size_t n, idlen;
	int offset;

	/*
	 * If the first non-blank char is a "{" then try to parse it as a
	 * relaxed JSON object. Otherwise try to parse it as a literal and
	 * convert to an id selector.
         */
	n = strspn(line, " \t");
	if (line[n] == '{') {
		offset = relaxed_to_strict((char *)doc, docsize, line, linelen,
		    1);
		if (offset == -1) {
			warnx("could not parse line as JSON object(s): %.*s",
			    (int)linelen, line);
			return -1;
		}

		return offset;
	}

	id = line + n;

	if (id[0] == '"') {
		id++;
		idlen = strcspn(id, "\"");

		if (idlen == 0) {
			warnx("could not parse selector as double quoted id: "
			    "\"%.*s\"", (int)linelen, line);
			return -1;
		}
	} else if (id[0] == '\'') {
		id++;
		idlen = strcspn(id, "'");

		if (idlen == 0) {
			warnx("could not parse selector as single quoted id: "
			    "\"%.*s\"", (int)linelen, line);
			return -1;
		}
	} else {
		idlen = strcspn(id, " \t");

		if (idlen == 0) {
			if (docsize > 0)
				doc[0] = '\0';

			return 0;
		}
	}

	if (idtosel((char *)doc, docsize, id, idlen) == -1) {
		warnx("could not parse selector as an id: \"%.*s\"", (int)idlen,
		    id);
		return -1;
	}

	return (id - line) + idlen;
}

static char *
prompt(void)
{
	return pmpt;
}

/*
 * Update the prompt with the given dbname and collname. If the prompt exceeds
 * MAXPROMPTCOLUMNS than shorten the dbname and collname.
 *
 * The following cases can arise:
 * 1. dbname and collname are NULL, then prompt will be "/> "
 * 2. dbname is not NULL and collname is NULL:
 *   a. if columns("/> ") + columns(dbname) is <= MAXPROMPTCOLUMNS
 *      then the prompt will be "/dbname> "
 *   b. if columns("/> ") + columns(dbname) is > MAXPROMPTCOLUMNS
 *      then dbname will be shortened and the prompt will be "/db..me> "
 * 3. dbname is not NULL and collname is not NULL:
 *   a. if columns("//> ") + columns(dbname) + columns(collname) is <= MAXPROMPTCOLUMNS
 *      then the prompt will be "/dbname/collname> "
 *   b. if columns("//> ") + columns(dbname) + columns(collname) is > MAXPROMPTCOLUMNS
 *      then dbname and collname will be shortened and the prompt will be
 *      "/db..me/co..me> ".
 */
static int
set_prompt(const char *dbname, const char *collname)
{
	char c1[sizeof(pmpt)], c2[sizeof(pmpt)];
	size_t n;

	if (dbname == NULL && collname == NULL) {
		if ((size_t)snprintf(pmpt, sizeof(pmpt), "/> ") >= sizeof(pmpt))
			return -1;
	}

	if (dbname == NULL) {
		/* only collname is provided */
		return -1;
	}

	c1[0] = '\0';
	c2[0] = '\0';

	/* default to db only prompt */
	n = strlen("/> ");
	if (strlcpy(c1, dbname, sizeof(c1)) >= sizeof(c1))
		return -1;

	if (collname != NULL) {
		/* make dbname + collname prompt */
		n = strlen("//> ");
		if (strlcpy(c2, collname, sizeof(c2)) >= sizeof(c2))
			return -1;
	}

	if (shorten_comps(c1, c2, MAXPROMPTCOLUMNS - n) == (size_t)-1)
		return -1;

	if (collname == NULL) {
		n = snprintf(pmpt, sizeof(pmpt), "/%s> ", c1);
		if (n >= sizeof(pmpt))
			return -1;
	} else {
		n = snprintf(pmpt, sizeof(pmpt), "/%s/%s> ", c1, c2);
		if (n >= sizeof(pmpt))
			return -1;
	}

	return 0;
}

/*
 * List database for the given client.
 *
 * Return 0 on success, -1 on failure.
 */
static int
exec_lsdbs(mongoc_client_t * client, const char *prefix)
{
	bson_error_t error;
	char **strv;
	int i, prefixlen;

	if (prefix != NULL)
		prefixlen = strlen(prefix);

	strv = mongoc_client_get_database_names_with_opts(client, NULL, &error);
	if (strv == NULL) {
		warnx("cursor failed: %d.%d %s", error.domain, error.code,
		      error.message);
		return -1;
	}

	for (i = 0; strv[i]; i++) {
		if (prefix == NULL) {
			printf("%s\n", strv[i]);
		} else {
			if (strncmp(prefix, strv[i], prefixlen) == 0)
				printf("%s\n", strv[i]);
		}
	}

	bson_strfreev(strv);
	strv = NULL;

	return 0;
}

/*
 * List collections for the given database.
 *
 * Return 0 on success, -1 on failure.
 */
static int
exec_lscolls(mongoc_client_t *client, char *dbname)
{
	bson_error_t error;
	mongoc_database_t *db;
	char **strv;
	int i;

	if (strlen(dbname) == 0)
		return -1;

	db = mongoc_client_get_database(client, dbname);
	strv = mongoc_database_get_collection_names_with_opts(db, NULL, &error);
	mongoc_database_destroy(db);
	db = NULL;
	if (strv == NULL) {
		warnx("lscolls failed %d.%d %s", error.domain, error.code,
		    error.message);
		return -1;
	}

	for (i = 0; strv[i]; i++)
		printf("%s\n", strv[i]);

	bson_strfreev(strv);
	strv = NULL;

	return 0;
}

/*
 * Execute a query.
 *
 * Return 0 on success, -1 on failure.
 */
static int
exec_query(mongoc_collection_t * collection, const char *line, size_t linelen,
   int idsonly)
{
	mongoc_cursor_t *cursor;
	bson_error_t error;
	size_t rlen;
	const bson_t *doc;
	char *str;
	bson_t *query, *fields;
	struct winsize w;

	if (sizeof(tmpdocs) < 3)
		abort();

	if (parse_selector(tmpdocs, sizeof(tmpdocs), line, linelen) == -1)
		return -1;

	/* default to all documents */
	if (strlen((char *)tmpdocs) == 0) {
		tmpdocs[0] = '{';
		tmpdocs[1] = '}';
		tmpdocs[2] = '\0';
	}

	if ((query = bson_new_from_json(tmpdocs, -1, &error)) == NULL) {
		warnx("%d.%d %s", error.domain, error.code, error.message);
		return -1;
	}

	if (idsonly) {
		if ((fields = bson_new_from_json(
		    (uint8_t *)"{ \"projection\": { \"_id\": true } }", -1,
		    &error)) == NULL) {
			warnx("%d.%d %s", error.domain, error.code,
			    error.message);
			bson_destroy(query);
			return -1;
		}
	}

	cursor = mongoc_collection_find_with_opts(collection, query,
	    idsonly ? fields : NULL, NULL);

	bson_destroy(query);
	if (idsonly)
		bson_destroy(fields);

	w.ws_row = 0;
	w.ws_col = 0;
	if (hr && ttyout) {
		if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1)
			warn("could not determine window size: %d %d", w.ws_row,
			    w.ws_col);
	}

	while (mongoc_cursor_next(cursor, &doc)) {
		if (hr) {
			str = bson_as_relaxed_extended_json(doc, &rlen);
		} else {
			str = bson_as_canonical_extended_json(doc, &rlen);
		}

		if (hr && rlen > w.ws_col) {
			if (human_readable((char *)tmpdocs, sizeof(tmpdocs),
			    str, rlen) == -1) {
				warnx("could not make human readable JSON "
				    "string");

				return -1;
			}
			printf("%s\n", tmpdocs);
		} else {
			printf("%s\n", str);
		}

		bson_free(str);
	}

	if (mongoc_cursor_error(cursor, &error)) {
		warnx("cursor failed: %d.%d %s", error.domain, error.code,
		    error.message);
		mongoc_cursor_destroy(cursor);
		cursor = NULL;
		return -1;
	}

	mongoc_cursor_destroy(cursor);
	cursor = NULL;

	return 0;
}

static int
exec_ls(const char *npath)
{
	int rc;
	path_t tmppath;
	char p[PATH_MAX];
	mongoc_collection_t *ccoll;

	/* copy current context */
	if ((size_t)snprintf(p, sizeof(p), "/%s/%s", path.dbname, path.collname)
	    >= sizeof(p)) {
		warnx("exec_ls p too small: %s", npath);
		return -1;
	}

	if (resolvepath(p, sizeof(p), npath, NULL) == (size_t)-1) {
		warnx("exec_ls resolvepath error: %s", npath);
		return -1;
	}

	if (parse_path(&tmppath, p) == -1) {
		warnx("parse_path error: %s", npath);
		abort();
	}

	if (strlen(tmppath.collname) > 0) {
		/* print all document ids */
		ccoll = mongoc_client_get_collection(client, tmppath.dbname,
		    tmppath.collname);
		rc = exec_query(ccoll, "{}", 2, 1);
		mongoc_collection_destroy(ccoll);
		ccoll = NULL;
		return rc;
	} else if (strlen(tmppath.dbname) > 0) {
		return exec_lscolls(client, tmppath.dbname);
	}

	return exec_lsdbs(client, NULL);
}

static int
exec_drop(const char *npath)
{
	char p[PATH_MAX];
	path_t tmppath;
	mongoc_collection_t *coll;
	mongoc_database_t *db;
	bson_error_t error;

	/* copy current context */
	if ((size_t)snprintf(p, sizeof(p), "/%s/%s", path.dbname, path.collname)
	    >= sizeof(p)) {
		warnx("p too small: %s", npath);
		return -1;
	}

	if (resolvepath(p, sizeof(p), npath, NULL) == (size_t)-1) {
		warnx("resolvepath error: %s", npath);
		return -1;
	}

	if (parse_path(&tmppath, p) == -1) {
		warnx("parse_path error: %s", npath);
		abort();
	}

	if (strlen(tmppath.collname) > 0) {	/* drop collection */
		coll = mongoc_client_get_collection(client, tmppath.dbname,
		    tmppath.collname);
		if (!mongoc_collection_drop(coll, &error)) {
			warnx("cursor failed: %d.%d %s", error.domain,
			    error.code, error.message);
			mongoc_collection_destroy(coll);
			coll = NULL;
			return -1;
		}
		mongoc_collection_destroy(coll);
		coll = NULL;
		printf("dropped /%s/%s\n", tmppath.dbname, tmppath.collname);
	} else if (strlen(tmppath.dbname) > 0) {
		db = mongoc_client_get_database(client, tmppath.dbname);
		if (!mongoc_database_drop(db, &error)) {
			warnx("cursor failed: %d.%d %s", error.domain,
			    error.code, error.message);
			mongoc_database_destroy(db);
			db = NULL;
			return -1;
		}
		mongoc_database_destroy(db);
		db = NULL;
		printf("dropped %s\n", tmppath.dbname);
	} else {
		/* illegal context */
		return -1;
	}

	return 0;
}

/*
 * Change dbname and/or collname, set ccoll and update prompt.
 *
 * Return 0 on success, -1 on failure.
 */
static int
exec_chcoll(mongoc_client_t *client, const path_t newpath)
{
	/* unset current collection */
	if (ccoll != NULL) {
		mongoc_collection_destroy(ccoll);
		ccoll = NULL;
	}
	/* if there is a new collection, change to it */
	if (strlen(newpath.dbname) > 0 && strlen(newpath.dbname) > 0)
		ccoll = mongoc_client_get_collection(client, newpath.dbname,
		    newpath.collname);

	/* update prompt to show whatever we've changed to */
	if (set_prompt(newpath.dbname, newpath.collname) == -1)
		warnx("can't update prompt with db and collection name");

	/* update global references */
	if (strlcpy(prevpath.dbname, path.dbname, MAXDBNAME) >= MAXDBNAME)
		return -1;
	if (strlcpy(prevpath.collname, path.collname, MAXCOLLNAME) >=
	    MAXCOLLNAME)
		return -1;
	if (strlcpy(path.dbname, newpath.dbname, MAXDBNAME) >= MAXDBNAME)
		return -1;
	if (strlcpy(path.collname, newpath.collname, MAXCOLLNAME) >=
	    MAXCOLLNAME)
		return -1;

	return 0;
}

/*
 * Count number of documents in collection.
 *
 * Return 0 on success, -1 on failure.
 */
static int
exec_count(mongoc_collection_t *collection, const char *line, size_t linelen)
{
	bson_error_t error;
	bson_t *query;
	int64_t count;

	if (sizeof(tmpdocs) < 3)
		abort();

	if (parse_selector(tmpdocs, sizeof(tmpdocs), line, linelen) == -1)
		return -1;

	/* default to all documents */
	if (strlen((char *)tmpdocs) == 0) {
		tmpdocs[0] = '{';
		tmpdocs[1] = '}';
		tmpdocs[2] = '\0';
	}

	if ((query = bson_new_from_json(tmpdocs, -1, &error)) == NULL) {
		warnx("%d.%d %s", error.domain, error.code, error.message);
		return -1;
	}

	count = mongoc_collection_count_documents(collection, query, NULL, NULL,
	    NULL, &error);

	bson_destroy(query);

	if (count == -1) {
		warnx("cursor failed: %d.%d %s", error.domain, error.code,
		    error.message);
		return -1;
	}

	printf("%ld\n", count);

	return 0;
}

/*
 * Parse update command, expect two json objects, a selector, and an update
 * doc.
 */
static int
exec_update(mongoc_collection_t *collection, const char *line, size_t linelen,
    int upsert)
{
	uint8_t update_docs[MAXDOC];
	uint8_t *update_doc = update_docs;
	bson_error_t error;
	bson_t *query, *update;
	int offset;
	int opts = MONGOC_UPDATE_NONE;

	if (upsert)
		opts |= MONGOC_UPDATE_UPSERT;

	/* expect two json objects */
	offset = parse_selector(tmpdocs, sizeof(tmpdocs), line, linelen);
	if (offset <= 0)
		return -1;

	line += offset;
	linelen -= offset;

	offset = relaxed_to_strict((char *)update_doc, MAXDOC, line, linelen,
	    1);
	if (offset <= 0)
		return -1;

	line += offset;
	linelen -= offset;

	if ((query = bson_new_from_json(tmpdocs, -1, &error)) == NULL) {
		warnx("%d.%d %s", error.domain, error.code, error.message);
		return -1;
	}

	if ((update = bson_new_from_json(update_doc, -1, &error)) == NULL) {
		warnx("%d.%d %s", error.domain, error.code, error.message);
		goto cleanuperr;
	}

	if (!mongoc_collection_update(collection,
	    opts | MONGOC_UPDATE_MULTI_UPDATE, query, update, NULL, &error)) {
		/*
		 * if error is "multi update only works with $ operators", retry
		 * without MULTI
		 */
		if (error.domain == MONGOC_ERROR_COMMAND &&
		    error.code == MONGOC_ERROR_CLIENT_TOO_SMALL) {
			if (!mongoc_collection_update(collection, opts, query,
			    update, NULL, &error)) {
				warnx("%d.%d %s", error.domain, error.code,
				    error.message);
				goto cleanuperr;
			}
		} else {
			warnx("%d.%d %s", error.domain, error.code,
			    error.message);
			goto cleanuperr;
		}
	}

	bson_destroy(query);
	bson_destroy(update);

	return 0;

cleanuperr:
	bson_destroy(query);
	bson_destroy(update);

	return -1;
}

/* parse insert command, expect one json object, the insert doc and exec */
static int
exec_insert(mongoc_collection_t *collection, const char *line, size_t linelen)
{
	bson_error_t error;
	bson_t *doc;
	int offset;

	offset = parse_selector(tmpdocs, sizeof(tmpdocs), line, linelen);
	if (offset <= 0)
		return -1;

	if ((doc = bson_new_from_json(tmpdocs, -1, &error)) == NULL) {
		warnx("%d.%d %s", error.domain, error.code, error.message);
		return -1;
	}

	/* execute insert */
	if (!mongoc_collection_insert(collection, MONGOC_INSERT_NONE, doc, NULL,
	    &error)) {
		warnx("%d.%d %s", error.domain, error.code, error.message);
		bson_destroy(doc);
		return -1;
	}

	bson_destroy(doc);

	return 0;
}

/* parse remove command, expect one selector */
static int
exec_remove(mongoc_collection_t *collection, const char *line, size_t linelen)
{
	int offset;
	bson_error_t error;
	bson_t *doc;

	offset = parse_selector(tmpdocs, sizeof(tmpdocs), line, linelen);
	if (offset <= 0)
		return -1;

	if ((doc = bson_new_from_json(tmpdocs, -1, &error)) == NULL) {
		warnx("%d.%d %s", error.domain, error.code, error.message);
		return -1;
	}

	if (!mongoc_collection_remove(collection, MONGOC_REMOVE_NONE, doc, NULL,
	    &error)) {
		warnx("%d.%d %s", error.domain, error.code, error.message);
		bson_destroy(doc);
		return -1;
	}

	bson_destroy(doc);

	return 0;
}

/*
 * Execute an aggregation pipeline.
 *
 * Return 0 on success, -1 on failure.
 */
static int
exec_agquery(mongoc_collection_t *collection, const char *line, size_t linelen)
{
	bson_error_t error;
	bson_t *aggr_query;
	mongoc_cursor_t *cursor;
	const bson_t *doc;
	char *str;

	if (sizeof(tmpdocs) < 3)
		abort();

	if (relaxed_to_strict((char *)tmpdocs, sizeof(tmpdocs), line, linelen,
	    0) == -1) {
		warnx("could not parse line as JSON object(s): %.*s",
		    (int)linelen, line);
		return -1;
	}

	/* default to all documents */
	if (strlen((char *)tmpdocs) == 0) {
		tmpdocs[0] = '[';
		tmpdocs[1] = ']';
		tmpdocs[2] = '\0';
	}

	if ((aggr_query = bson_new_from_json(tmpdocs, -1, &error)) == NULL) {
		warnx("%d.%d %s", error.domain, error.code, error.message);
		return -1;
	}

	cursor = mongoc_collection_aggregate(collection, MONGOC_QUERY_NONE,
	    aggr_query, NULL, NULL);

	bson_destroy(aggr_query);

	while (mongoc_cursor_next(cursor, &doc)) {
		if (hr) {
			str = bson_as_relaxed_extended_json(doc, NULL);
		} else {
			str = bson_as_canonical_extended_json(doc, NULL);
		}
		printf("%s\n", str);
		bson_free(str);
	}

	if (mongoc_cursor_error(cursor, &error)) {
		warnx("cursor failed: %d.%d %s", error.domain, error.code,
		    error.message);
		mongoc_cursor_destroy(cursor);
		cursor = NULL;
		return -1;
	}

	mongoc_cursor_destroy(cursor);
	cursor = NULL;

	return 0;
}

/*
 * Execute command with given arguments.
 *
 * Return 0 on success, -1 on failure.
 */
static int
exec_cmd(const char *cmd, const char *allcmds[], const char *line, size_t linelen)
{
	char p[PATH_MAX];
	const char *arg1, *arg2;
	char *arg;
	size_t i, arg1len, arg2len;
	path_t tmppath;

	arg1 = line;
	arg1len = nexttok(&arg1);

	arg2len = 0;
	if (arg1len > 0) {
		arg2 = &arg1[arg1len];
		arg2len = nexttok(&arg2);
	}

	if (strcmp("cd", cmd) == 0) {
		if (arg1len == 0) {
			warnx("cd requires one path argument: cd path");
			return -1;
		}

		if (arg2len > 0) {
			warnx("cd requires only one path argument: cd path");
			return -1;
		}

		/* special case "cd -" */
		if (arg1len == 1 && arg1[0] == '-') {
			if (strlcpy(tmppath.dbname, prevpath.dbname, MAXDBNAME)
			    >= MAXDBNAME)
				return -1;
			if (strlcpy(tmppath.collname, prevpath.collname,
			    MAXCOLLNAME) >= MAXCOLLNAME)
				return -1;
		} else {
			if ((size_t)snprintf(p, sizeof(p), "/%s/%s",
			    path.dbname, path.collname) >= sizeof(p)) {
				warnx("p too small: %s", line);
				return -1;
			}

			arg = strndup(arg1, arg1len);
			if (arg == NULL) {
				warn("exec_cmd strndup");
				abort();
			}

			if (resolvepath(p, sizeof(p), arg, NULL) == (size_t)-1)
			    {
				warnx("resolvepath error: %s", line);
				free(arg);
				return -1;
			}
			free(arg);

			if (parse_path(&tmppath, p) == -1) {
				warnx("parse_path error: %s", line);
				abort();
			}
		}

		return exec_chcoll(client, tmppath);
	}

	if (strcmp("help", cmd) == 0) {
		for (i = 0; allcmds[i] != NULL; i++)
			printf("%s\n", allcmds[i]);

		return 0;
	}

	if (strcmp("ls", cmd) == 0) {
		if (arg2len > 0) {
			warnx("ls takes at most one argument: ls [path]");
			return -1;
		}

		return exec_ls(line);
	}

	if (strcmp("drop", cmd) == 0) {
		if (arg2len > 0) {
			warnx("drop takes at most one argument: ls [path]");
			return -1;
		}

		return exec_drop(line);
	}

	/*
	 * All the other commands need a database and collection to be
	 * selected.
         */
	if (strlen(path.dbname) == 0) {
		warnx("no database selected");
		return -1;
	}

	if (strlen(path.collname) == 0) {
		warnx("no collection selected");
		return -1;
	}

	if (strcmp("count", cmd) == 0) {
		return exec_count(ccoll, line, linelen);
	} else if (strcmp("update", cmd) == 0) {
		return exec_update(ccoll, line, linelen, 0);
	} else if (strcmp("upsert", cmd) == 0) {
		return exec_update(ccoll, line, linelen, 1);
	} else if (strcmp("insert", cmd) == 0) {
		return exec_insert(ccoll, line, linelen);
	} else if (strcmp("remove", cmd) == 0) {
		return exec_remove(ccoll, line, linelen);
	} else if (strcmp("find", cmd) == 0) {
		return exec_query(ccoll, line, linelen, 0);
	} else if (strcmp("aggregate", cmd) == 0) {
		return exec_agquery(ccoll, line, linelen);
	}

	warnx("unknown command: \"%s\"", line);
	return -1;
}

/*
 * Load the first line from ~/mongovi into "line".
 *
 * Return 0 on success, -1 on failure.
 */
static int
loaddotfile(char *line, size_t linelen)
{
	struct stat st;
	struct passwd *pw;
	FILE *fp;
	size_t n;

	if ((pw = getpwuid(getuid())) == NULL) {
		warn("could not load ~/%s: getpwuid failed", DOTFILE);
		return -1;
	}

	n = snprintf(line, linelen, "%s/%s", pw->pw_dir, DOTFILE);
	if (n >= linelen) {
		warnx("could not load ~/%s: path to homedir too long: %s",
		    DOTFILE, pw->pw_dir);
		return -1;
	}

	if ((fp = fopen(line, "r")) == NULL) {
		if (errno == ENOENT)
			return -1;

		warn("could not load %s", line);
		return -1;
	}

	if (fstat(fileno(fp), &st) == -1) {
		warn("could stat %s", line);
		fclose(fp);
		return -1;
	}

	if (st.st_mode & (S_IROTH | S_IWOTH)) {
		warnx("~/%s not used because it is readable and/or writable by "
		    "others\n\trun `chmod o-rw %s` to fix permissions", DOTFILE,
		    line);
		fclose(fp);
		return -1;
	}

	if (fgets(line, linelen, fp) == NULL) {
		if (ferror(fp)) {
			warn("could not read first line of ~/%s", DOTFILE);
			fclose(fp);
			return -1;
		}
	}

	fclose(fp);
	line[strcspn(line, "\n")] = '\0';

	return 0;
}

/*
 * Handle special import mode, treat each input line as one MongoDB Extended
 * JSON document and force insert command.
 *
 * Returns the number of inserted objects on success, or -1 on error with errno
 * set.
 */
static int
do_import(mongoc_collection_t * collection)
{
	bson_error_t error;
	bson_t *docs[BULKINSERTMAX];
	char *line;
	ssize_t r;
	size_t n, j;
	int i;

	for (j = 0; j < BULKINSERTMAX; j++)
		docs[j] = bson_new();

	line = NULL;
	n = 0;
	i = 0;
	j = 0;
	while ((r = getline(&line, &n, stdin)) != -1) {
		if (r > 0 && line[r - 1] == '\n') {
			line[r - 1] = '\0';
			r--;
		}

		if (r == 0)
			continue;

		if (bson_init_from_json(docs[j], line, r, &error) == false) {
			warnx("JSON error: %d.%d %s", error.domain, error.code,
			    error.message);
			continue;
		}

		j++;

		if (j == BULKINSERTMAX) {
			if (mongoc_collection_insert_many(collection,
			    (const bson_t **)docs, j, NULL, NULL, &error) ==
			    false) {
				warnx("insert error: %d.%d %s", error.domain,
				    error.code, error.message);
				continue;
			}
			i += j;
			j = 0;
		}
	}

	if (j > 0) {
		if (mongoc_collection_insert_many(collection,
		    (const bson_t **)docs, j, NULL, NULL, &error) == false) {
			warnx("insert error: %d.%d %s", error.domain,
			    error.code, error.message);
			i = -1;
			goto exit;
		}

		i += j;
		j = 0;
	}

	if (ferror(stdin) != 0) {
		i = -1;
		goto exit;
	}

exit:
	free(line);
	for (j = 0; j < BULKINSERTMAX; j++)
		bson_destroy(docs[j]);

	return i;
}

static void
printversion(int d)
{
	dprintf(d, "%s v%d.%d.%d\n", progname, VERSION_MAJOR,
	    VERSION_MINOR, VERSION_PATCH);
}

static void
printusage(int d)
{
	dprintf(d, "usage: %s [-p] [/database/collection]\n", progname);
	dprintf(d, "       %s [-s] [/database/collection]\n", progname);
	dprintf(d, "       %s -i /database/collection\n", progname);
	dprintf(d, "       %s -V\n", progname);
	dprintf(d, "       %s -h\n", progname);
}

int
main(int argc, char **argv)
{
	const wchar_t *line;
	const char *cmd, *args;
	char p[PATH_MAX];
	char connurl[MAXMONGOURL];
	char linecpy[MAXLINE], *lp;
	size_t n;
	int i, read, c;
	EditLine *e;
	History *h;
	HistEvent he;
	path_t newpath = {"", ""};

	setlocale(LC_CTYPE, "");

	assert((MB_CUR_MAX) > 0 && (MB_CUR_MAX) < 8);

	if (strlcpy(progname, basename(argv[0]), MAXPROG) >= MAXPROG)
		errx(1, "program name too long: %s", argv[0]);

	if (isatty(STDIN_FILENO))
		ttyin = 1;

	if (isatty(STDOUT_FILENO))
		ttyout = 1;

	/* default ttys to human readable output */
	if (ttyout)
		hr = 1;

	while ((c = getopt(argc, argv, "Vhips")) != -1) {
		switch (c) {
		case 'p':
			hr = 1;
			break;
		case 's':
			hr = 0;
			break;
		case 'i':
			import = 1;
			break;
		case 'V':
			printversion(STDOUT_FILENO);
			exit(0);
		case 'h':
			printusage(STDOUT_FILENO);
			exit(0);
		case '?':
			printusage(STDERR_FILENO);
			exit(1);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc > 1) {
		printusage(STDERR_FILENO);
		exit(1);
	}

	if (PATH_MAX < 20)
		errx(1, "can't determine PATH_MAX");

	if (loaddotfile(connurl, sizeof(connurl)) == -1) {
		if (strlcpy(connurl, DFLMONGOURL, sizeof(connurl)) >=
		    sizeof(connurl))
			errx(1, "url in config too long");
	}

	/* setup mongo */
	mongoc_init();
	if ((client = mongoc_client_new(connurl)) == NULL)
		errx(1, "can't connect to mongo using connection string \"%s\"",
		    connurl);

	if (argc == 1) {
		p[0] = '/';
		p[1] = '\0';
		if (resolvepath(p, sizeof(p), argv[0], NULL) == (size_t)-1)
			errx(1, "resolvepath error: %s", argv[0]);

		if (parse_path(&newpath, p) == -1)
			errx(1, "parse_path error: %s", argv[0]);

		if (exec_chcoll(client, newpath) < 0)
			errx(1, "can't change database or collection");
	}

	/*
	 * Handle special import mode, expect one extended json object per
	 * line.
	 */
	if (import) {
		if (ccoll == NULL)
			errx(1, "database/collection path required in import mode");

		i = do_import(ccoll);
		if (i == -1)
			err(1, NULL);

		printf("inserted %d documents\n", i);

		mongoc_collection_destroy(ccoll);
		ccoll = NULL;

		mongoc_client_destroy(client);
		client = NULL;

		mongoc_cleanup();

		exit(0);
	}

	if ((e = el_init(progname, stdin, stdout, stderr)) == NULL)
		errx(1, "can't initialize editline");

	if ((h = history_init()) == NULL)
		errx(1, "can't initialize history");

	history(h, &he, H_SETSIZE, 100);
	el_set(e, EL_HIST, history, h);

	el_set(e, EL_PROMPT, prompt);
	el_set(e, EL_EDITOR, "emacs");
	el_set(e, EL_TERMINAL, NULL);

	/* load user defaults */
	if (el_source(e, NULL) == -1)
		warnx("sourcing .editrc failed");

	if (el_get(e, EL_EDITMODE, &i) != 0)
		errx(1, "can't determine editline status");

	if (i == 0)
		errx(1, "editline disabled");

	el_set(e, EL_ADDFN, "complete", "Context sensitive argument completion",
	    complete);

	el_set(e, EL_BIND, "\t", "complete", NULL);

	while ((line = el_wgets(e, &read)) != NULL) {
		linecpy[0] = '\0';
		n = wcstombs(linecpy, line, sizeof(linecpy));
		if (n == (size_t)-1 || n == sizeof(linecpy)) {
			warnx("linecpy too short: %ld bytes needed, have %ld",
			    wcstombs(NULL, line, 0) + 1, sizeof(linecpy));
			continue;
		}

		if (n == 0)
			continue;

		/*
		 * Trim trailing newline if any (might error on exotic, non-C
		 * and non-UTF8 locales).
		 */
		if (linecpy[n - 1] == '\n') {
			linecpy[n - 1] = '\0';
			n--;
		}

		if (n == 0)
			continue;

		if (history(h, &he, H_ENTER, linecpy) == -1)
			warnx("can't enter history");

		/*
		 * Parse command and let args point to the first token after
		 * the command.
		 */
		lp = linecpy;
		n = nexttok((const char **)&lp);

		if (n == 0)
			continue;

		cmd = lp;
		if (lp[n] == '\0') {
			args = "";
		} else {
			args = &lp[n];
			nexttok(&args);
			lp[n] = '\0';
		}

		i = complete_word(e, cmd, n, cmds, &cmd);
		if (i == 0) {
			warnx("unknown command: \"%s\"", cmd);
			continue;
		}

		if (exec_cmd(cmd, cmds, args, strlen(args)) == -1)
			warnx("execution failed");
	}

	if (read == -1)
		err(1, NULL);

	mongoc_collection_destroy(ccoll);
	ccoll = NULL;

	mongoc_client_destroy(client);
	client = NULL;

	mongoc_cleanup();

	history_end(h);
	el_end(e);

	if (ttyin)
		printf("\n");

	return 0;
}
