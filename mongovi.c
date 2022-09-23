/**
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

#define MAXMONGOURL 200

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

/* shell specific user info */
typedef struct {
	char name[MAXUSERNAME];
	char home[PATH_MAX];
} user_t;

/* mongo specific db info */
typedef struct {
	char url[MAXMONGOURL];
} config_t;

enum cmd {
	ILLEGAL =
	-1, UNKNOWN, AMBIGUOUS, DROP, LS, CHCOLL, COUNT, UPDATE, UPSERT,
	INSERT, REMOVE, FIND, AGQUERY, HELP
};
enum errors {
	DBMISSING = 256, COLLMISSING
};

static char progname[MAXPROG];

static path_t path, prevpath;

/* use as temporary one-time storage while building a query or query results */
static uint8_t tmpdocs[16 * 1024 * 1024];

static user_t user;
static config_t config;
static char **list_match = NULL;/* contains all ambiguous prefix_match
				   commands */

/*
 * Make sure the prompt can hold MAXPROMPTCOLUMNS + a trailing null. Since
 * MB_CUR_MAX is only defined after setlocale() is executed, we assume it is
 * less than 8 bytes.
 */
static char pmpt[8 * MAXPROMPTCOLUMNS + 8] = "/> ";

static mongoc_client_t *client;
static mongoc_collection_t *ccoll = NULL;	/* current collection */

/* print human readable or not */
int hr = 0;

/*
 * import mode, treat each input line as one MongoDB Extended JSON document and
 * force insert command.
 */
int import = 0;

#define NCMDS (sizeof cmds / sizeof cmds[0])
#define MAXCMDNAM (sizeof cmds)	/* broadly define maximum length of a command
				   name */

const char *cmds[] = {
	"aggregate",		/* AGQUERY */
	"cd",			/* CHCOLL,  change database and/or collection */
	"count",		/* COUNT */
	"drop",			/* DROP */
	"find",			/* FIND */
	"help",			/* print usage */
	"insert",		/* INSERT */
	"ls",			/* LS */
	"remove",		/* REMOVE */
	"update",		/* UPDATE */
	"upsert",		/* UPSERT */
	NULL			/* nul terminate this list */
};

/*
 * list database for the given client return 0 on success, -1 on failure
 */
int
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

	return 0;
}

/*
 * list collections for the given database return 0 on success, -1 on failure
 */
int
exec_lscolls(mongoc_client_t * client, char *dbname)
{
	bson_error_t error;
	mongoc_database_t *db;
	char **strv;
	int i;

	if (!strlen(dbname))
		return -1;

	db = mongoc_client_get_database(client, dbname);

	if ((strv =
	     mongoc_database_get_collection_names_with_opts(db, NULL,
							    &error)) == NULL)
		return -1;

	for (i = 0; strv[i]; i++)
		printf("%s\n", strv[i]);

	bson_strfreev(strv);
	mongoc_database_destroy(db);

	return 0;
}

/*
 * tab complete command
 *
 * if matches more than one command, print all
 * if matches exactly one command and not complete, complete
 *
 * return 0 on success or -1 on failure
 */
int
complete_cmd(EditLine * e, const char *tok, int co)
{
	char *cmd;		/* completed command */
	int i;
	size_t cmdlen;

	/* check if cmd matches one or more commands */
	if (prefix_match(&list_match, cmds, tok) == -1)
		errx(1, "prefix_match error");

	/* unknown prefix */
	if (list_match[0] == NULL)
		return 0;

	/* matches more than one command, print list_match */
	if (list_match[1] != NULL) {
		i = 0;
		printf("\n");
		while (list_match[i] != NULL)
			printf("%s\n", list_match[i++]);

		/* ensure path is completed to the longest common prefix */
		i = common_prefix((const char **) list_match);
		cmd = strndup(list_match[0], i);
	} else {
		cmd = strdup(list_match[0]);
	}

	/* matches one command from cmds or has a common prefix */

	/*
         * complete the command if it's not complete yet but only if the cursor
         * is on a blank
         */
	cmdlen = strlen(cmd);
	if (cmdlen >= strlen(tok)) {
		switch (tok[co]) {
		case ' ':
		case '\0':
		case '\n':
		case '\t':
			if (cmdlen > strlen(tok))
				if (el_insertstr(e, cmd + strlen(tok)) < 0) {
					free(cmd);
					return -1;
				}
			/* append " " if exactly one command matched */
			if (list_match[1] == NULL)
				if (el_insertstr(e, " ") < 0) {
					free(cmd);
					return -1;
				}
			break;
		}
	}
	free(cmd);
	return 0;
}

/*
 * Tab complete path. Relative paths depend on current context.
 *
 * if empty, print all possible arguments
 * if matches more than one component, print all with matching prefix and zip
 * up
 * if matches exactly one component and not complete, complete
 *
 * npath is the new path, it should not contain any blanks
 * cp is cursor position in npath
 * return 0 on success or -1 on failure
 */
int
complete_path(EditLine * e, const char *npath, int cp)
{
	enum complete {
		CDB, CCOLL
	};
	path_t tmppath;
	char *c, *found;
	int i, j, k, ret;
	bson_error_t error;
	char **strv;
	char **matches = NULL;
	size_t pathlen;
	enum complete compl;
	mongoc_database_t *db;

	/* copy current context */
	if (strlcpy(tmppath.dbname, path.dbname, MAXDBNAME) >= MAXDBNAME)
		return -1;
	if (strlcpy(tmppath.collname, path.collname, MAXCOLLNAME) >=
	    MAXCOLLNAME)
		return -1;

	if (parse_path(npath, &tmppath, &j, &k) < 0)
		errx(1, "illegal path spec");

	if (strlen(tmppath.collname))
		compl = CCOLL;
	else if (strlen(tmppath.dbname)) {
		/*
		 * Complete db, unless there is a dbname, followed by a "/" and the cursor
		 * is beyond it.
		 */
		compl = CDB;

		if (j >= 0) {	/* explicit dbname in npath */
			/* check if a "/" terminates the dbname */
			if ((c = strchr(npath + j, '/')) != NULL) {
				i = c - npath;
				if (i < cp)
					compl = CCOLL;
			}
		} else {	/* implicit dbname in npath */
			/*
	                 * if the cursor is on a blank and npath is either empty or
	                 * follows a .. or /, complete collection
	                 */
			switch (npath[cp]) {
			case ' ':
			case '\0':
			case '\n':
			case '\t':
				if (cp == 0 && npath[cp] == '\0') {	/* npath is empty */
					compl = CCOLL;
				} else if (cp > 0 && npath[cp - 1] == '/') {	/* implicit dbname,
										   possibly via "../" */
					compl = CCOLL;
				} else if (cp > 1 && npath[cp - 2] == '.'
					   && npath[cp - 1] == '.') {
					compl = CCOLL;
					/* ensure a trailing "/" */
					if (el_insertstr(e, "/") < 0)
						return -1;
				}
				break;
			}
		}
	} else
		compl = CDB;

	switch (compl) {
	case CDB:		/* complete database */
		/* if tmppath.dbname is empty, print all databases */
		if (!strlen(tmppath.dbname)) {
			printf("\n");
			ret = exec_lsdbs(client, NULL);
			/* ensure a trailing "/" */
			if (cp > 0 && npath[cp - 1] != '/')
				if (el_insertstr(e, "/") < 0)
					return -1;
			return ret;
		}
		/* otherwise get a list of matching prefixes */
		strv = mongoc_client_get_database_names_with_opts(client, NULL,
		    &error);
		if (strv == NULL)
			errx(1, "cd db failed %d.%d %s", error.domain,
			    error.code, error.message);

		/* check if this matches one or more entries */
		if (prefix_match(&matches, (const char **)strv, tmppath.dbname)
		    == -1)
			errx(1, "prefix_match error");

		/* unknown prefix */
		if (matches[0] == NULL)
			break;

		/* matches more than one entry */
		if (matches[1] != NULL) {
			i = 0;
			printf("\n");
			while (matches[i] != NULL)
				printf("%s\n", matches[i++]);

			/*
	                 * ensure path is completed to the longest common
	                 * prefix
	                 */
			i = common_prefix((const char **) matches);
			matches[0][i] = 0;
		}
		/* matches exactly one entry or common prefix */
		found = matches[0];

		/*
		 * complete the entry if it's not complete yet but only if the cursor
		 * is on a blank
		 */
		pathlen = strlen(found);
		if (pathlen >= strlen(tmppath.dbname)) {
			switch (npath[cp]) {
			case ' ':
			case '\0':
			case '\n':
			case '\t':
				if (pathlen > strlen(tmppath.dbname)) {
					if (el_insertstr(e, found + strlen(tmppath.dbname)) <
					    0) {
						free(matches);
						bson_strfreev(strv);
						return -1;
					}
				}
				/*
				 * if exactly one entry matched, ensure dbname ends with "/"
				 * and print collections
				 */
				if (matches[1] == NULL) {
					if (cp > 0 && npath[cp - 1] != '/')
						if (el_insertstr(e, "/") < 0) {
							free(matches);
							bson_strfreev(strv);
							return -1;
						}
					/* and print all collections */
					printf("\n");
					if (exec_lscolls(client, found) == -1) {
						free(matches);
						bson_strfreev(strv);
						return -1;
					}
				}
				break;
			}
		}
		break;
	case CCOLL:		/* complete collection */
		/* if tmppath.collname is empty, print all collections */
		if (!strlen(tmppath.collname)) {
			printf("\n");
			return exec_lscolls(client, tmppath.dbname);
		}
		/* otherwise get a list of matching prefixes */
		db = mongoc_client_get_database(client, tmppath.dbname);

		strv = mongoc_database_get_collection_names_with_opts(db, NULL,
		    &error);
		if (strv == NULL)
			errx(1, "cd coll failed %d.%d %s", error.domain,
			    error.code, error.message);

		mongoc_database_destroy(db);

		/* check if this matches one or more entries */
		if (prefix_match(&matches, (const char **)strv,
		    tmppath.collname) == -1)
			errx(1, "prefix_match error");

		/* unknown prefix */
		if (matches[0] == NULL)
			break;

		/* matches more than one entry */
		if (matches[1] != NULL) {
			i = 0;
			printf("\n");
			while (matches[i] != NULL)
				printf("%s\n", matches[i++]);

			/*
	                 * ensure path is completed to the longest common
	                 * prefix
	                 */
			i = common_prefix((const char **) matches);
			matches[0][i] = 0;
		}
		/* matches exactly one entry */
		found = matches[0];

		/*
		 * complete the entry if it's not complete yet but only if the cursor
		 * is on a blank or '/'
		 */
		pathlen = strlen(found);
		if (pathlen >= strlen(tmppath.collname)) {
			switch (npath[cp]) {
			case ' ':
			case '\0':
			case '\n':
			case '\t':
				if (pathlen > strlen(tmppath.collname)) {
					if (el_insertstr(e, found + strlen(tmppath.collname)) <
					    0) {
						free(matches);
						bson_strfreev(strv);
						return -1;
					}
				}
				/* append " " if exactly one command matched */
				if (matches[1] == NULL) {
					if (cp > 0 && npath[cp - 1] != '/')
						if (el_insertstr(e, " ") < 0) {
							free(matches);
							bson_strfreev(strv);
							return -1;
						}
				}
				break;
			}
		}
		break;
	default:
		errx(1, "unexpected completion");
	}

	free(matches);
	bson_strfreev(strv);

	return 0;
}

/*
 * tab complete command line
 *
 * if empty, print all commands
 * if matches more than one command, print all with matching prefix
 * if matches exactly one command and not complete, complete
 * if command is complete and needs args, look at that
 */
uint8_t
complete(EditLine * e, __attribute__((unused)) int ch)
{
	char cmd[MAXCMDNAM];
	Tokenizer *t;
	const char **av;
	int i, ret, ac, cc, co;

	/* default exit code to error */
	ret = CC_ERROR;

	/* tokenize */
	t = tok_init(NULL);
	if (tok_line(t, el_line(e), &ac, &av, &cc, &co) != 0)
		return ret;

	/* empty, print all commands */
	if (ac == 0) {
		i = 0;
		printf("\n");
		while (cmds[i] != NULL)
			printf("%s\n", cmds[i++]);
		ret = CC_REDISPLAY;
		goto cleanup;
	}
	/* init cmd */
	if (strlcpy(cmd, av[0], sizeof(cmd)) >= sizeof(cmd))
		goto cleanup;

	switch (cc) {
	case 0:		/* on command */
		if (complete_cmd(e, cmd, co) < 0)
			goto cleanup;
		ret = CC_REDISPLAY;
		break;
	case 1:		/* on argument, try to complete all commands
				   that support a path parameter */
		if (strcmp(cmd, "cd") == 0 || strcmp(cmd, "ls") == 0 ||
		    strcmp(cmd, "drop") == 0)
			if (complete_path(e, ac <= 1 ? "" : av[1], co) < 0) {
				warnx("complete_path error");
				goto cleanup;
			}
		ret = CC_REDISPLAY;
		goto cleanup;
	default:
		/* ignore subsequent words */
		ret = CC_NORM;
		goto cleanup;
	}

cleanup:
	tok_end(t);

	return ret;
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
 * Return 0 on success or -1 on error.
 */
int
idtosel(char *dst, const size_t dstsize, const char *sel, const size_t sellen)
{
	const size_t oidlen = 24;

	if (dstsize < 1 || sellen < 1)
		return -1;

	/* if 24 hex chars, assume an object id otherwise treat as a literal */
	if (sellen == oidlen && (strspn(sel, "0123456789abcdefABCDEF") == oidlen)) {
		if ((size_t)snprintf(dst, dstsize, "{ \"_id\": { \"$oid\": \"%.*s\" } }", (int)oidlen, sel) >= dstsize)
			return -1;
	} else {
		if ((size_t)snprintf(dst, dstsize, "{ \"_id\": \"%.*s\" }", (int)sellen, sel) >= dstsize)
			return -1;
	}

	return 0;
}

/*
 * Parse the selector in line, that is the first (relaxed) json object or
 * literal id. A strictly conforming JSON object is written in "doc" on
 * success.
 *
 * "line" must be null terminated and "len" must exclude the terminating null
 * byte.
 *
 * Return the number of bytes parsed on success or -1 on failure.
 * On success, if docsize > 0, a null byte is always written to doc.
 */
int
parse_selector(uint8_t *doc, const size_t docsize, const char *line, int len)
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
		offset = relaxed_to_strict((char *)doc, docsize, line, len, 1);
		if (offset == -1) {
			warnx("could not parse selector as a JSON object");
			return -1;
		}

		return offset;
	}

	id = line + n;

	if (id[0] == '"') {
		id++;
		idlen = strcspn(id, "\"");

		if (idlen == 0) {
			warnx("could not parse selector as double quoted id");
			return -1;
		}
	} else if (id[0] == '\'') {
		id++;
		idlen = strcspn(id, "'");

		if (idlen == 0) {
			warnx("could not parse selector as single quoted id");
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
		warnx("could not parse selector as an id");
		return -1;
	}

	return (id - line) + idlen;
}

/*
 * execute a query return 0 on success, -1 on failure
 */
int
exec_query(mongoc_collection_t * collection, const char *line, int len,
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

	if (parse_selector(tmpdocs, sizeof(tmpdocs), line, len) == -1)
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
			warnx("%d.%d %s", error.domain, error.code, error.message);
			bson_destroy(query);
			return -1;
		}
	}

	cursor = mongoc_collection_find_with_opts(collection, query,
	    idsonly ? fields : NULL, NULL);

	bson_destroy(query);
	if (idsonly)
		bson_destroy(fields);

	ioctl(0, TIOCGWINSZ, &w);

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
		return -1;
	}
	mongoc_cursor_destroy(cursor);

	return 0;
}

int
exec_ls(const char *npath)
{
	int ret;
	path_t tmppath;
	mongoc_collection_t *ccoll;

	/* copy current context */
	if (strlcpy(tmppath.dbname, path.dbname, MAXDBNAME) >= MAXDBNAME)
		return -1;
	if (strlcpy(tmppath.collname, path.collname, MAXCOLLNAME) >=
	    MAXCOLLNAME)
		return -1;

	if (parse_path(npath, &tmppath, NULL, NULL) < 0)
		errx(1, "illegal path spec");

	if (strlen(tmppath.collname)) {	/* print all document ids */
		ccoll =
		    mongoc_client_get_collection(client, tmppath.dbname,
						 tmppath.collname);
		ret = exec_query(ccoll, "{}", 2, 1);
		mongoc_collection_destroy(ccoll);
		return ret;
	} else if (strlen(tmppath.dbname))
		return exec_lscolls(client, tmppath.dbname);
	else
		return exec_lsdbs(client, NULL);
}

int
exec_drop(const char *npath)
{
	path_t tmppath;
	mongoc_collection_t *coll;
	mongoc_database_t *db;
	bson_error_t error;

	/* copy current context */
	if (strlcpy(tmppath.dbname, path.dbname, MAXDBNAME) >= MAXDBNAME)
		return -1;
	if (strlcpy(tmppath.collname, path.collname, MAXCOLLNAME) >=
	    MAXCOLLNAME)
		return -1;

	if (parse_path(npath, &tmppath, NULL, NULL) < 0)
		errx(1, "illegal path spec");

	if (strlen(tmppath.collname)) {	/* drop collection */
		coll =
		    mongoc_client_get_collection(client, tmppath.dbname,
						 tmppath.collname);
		if (!mongoc_collection_drop(coll, &error)) {
			warnx("cursor failed: %d.%d %s", error.domain, error.code,
			      error.message);
			mongoc_collection_destroy(coll);
			return -1;
		}
		mongoc_collection_destroy(coll);
		printf("dropped /%s/%s\n", tmppath.dbname, tmppath.collname);
	} else if (strlen(tmppath.dbname)) {
		db = mongoc_client_get_database(client, tmppath.dbname);
		if (!mongoc_database_drop(db, &error)) {
			warnx("cursor failed: %d.%d %s", error.domain, error.code,
			      error.message);
			mongoc_database_destroy(db);
			return -1;
		}
		mongoc_database_destroy(db);
		printf("dropped %s\n", tmppath.dbname);
	} else {
		/* illegal context */
		return -1;
	}

	return 0;
}

/* return command code */
int
mv_parse_cmd(int argc, const char *argv[], const char *line, char **lp)
{
	const char *cmd;

	/* check if the first token matches one or more commands */
	if (prefix_match(&list_match, cmds, argv[0]) == -1)
		errx(1, "prefix_match error");

	/* unknown prefix */
	if (list_match[0] == NULL)
		return UNKNOWN;

	/* matches more than one command */
	if (list_match[1] != NULL)
		return AMBIGUOUS;

	/* matches exactly one command from cmds */
	cmd = list_match[0];

	if (strcmp("cd", cmd) == 0) {
		*lp = strstr(line, argv[0]) + strlen(argv[0]);
		switch (argc) {
		case 2:
			return CHCOLL;
		default:
			return ILLEGAL;
		}
	} else if (strcmp("help", cmd) == 0) {
		*lp = strstr(line, argv[0]) + strlen(argv[0]);
		return HELP;
	}
	if (strcmp("ls", cmd) == 0) {
		*lp = strstr(line, argv[0]) + strlen(argv[0]);
		switch (argc) {
		case 1:
		case 2:
			return LS;
		default:
			return ILLEGAL;
		}
	}
	if (strcmp("drop", cmd) == 0) {
		*lp = strstr(line, argv[0]) + strlen(argv[0]);
		switch (argc) {
		case 1:
		case 2:
			return DROP;
		default:
			return ILLEGAL;
		}
	}
	/*
         * all the other commands need a database and collection to be
         * selected
         */
	if (!strlen(path.dbname))
		return DBMISSING;
	if (!strlen(path.collname))
		return COLLMISSING;

	if (strcmp("count", cmd) == 0) {
		*lp = strstr(line, argv[0]) + strlen(argv[0]);
		return COUNT;
	} else if (strcmp("update", cmd) == 0) {
		*lp = strstr(line, argv[0]) + strlen(argv[0]);
		return UPDATE;
	} else if (strcmp("upsert", cmd) == 0) {
		*lp = strstr(line, argv[0]) + strlen(argv[0]);
		return UPSERT;
	} else if (strcmp("insert", cmd) == 0) {
		*lp = strstr(line, argv[0]) + strlen(argv[0]);
		return INSERT;
	} else if (strcmp("remove", cmd) == 0) {
		*lp = strstr(line, argv[0]) + strlen(argv[0]);
		return REMOVE;
	} else if (strcmp("find", cmd) == 0) {
		*lp = strstr(line, argv[0]) + strlen(argv[0]);
		return FIND;
	} else if (strcmp("aggregate", cmd) == 0) {
		*lp = strstr(line, argv[0]) + strlen(argv[0]);
		return AGQUERY;
	}
	return UNKNOWN;
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
int
set_prompt(const char *dbname, const char *collname)
{
	char c1[sizeof(pmpt)], c2[sizeof(pmpt)];
	size_t fixedcolumns;

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
	fixedcolumns = strlen("/> ");
	if (strlcpy(c1, dbname, sizeof(c1)) >= sizeof(c1))
		return -1;

	if (collname != NULL) {
		/* make dbname + collname prompt */
		fixedcolumns = strlen("//> ");
		if (strlcpy(c2, collname, sizeof(c2)) >= sizeof(c2))
			return -1;
	}

	if (shorten_comps(c1, c2, MAXPROMPTCOLUMNS - fixedcolumns) == (size_t)-1)
		return -1;

	if (collname == NULL) {
		if ((size_t)snprintf(pmpt, sizeof(pmpt), "/%s> ", c1) >= sizeof(pmpt))
			return -1;
	} else {
		if ((size_t)snprintf(pmpt, sizeof(pmpt), "/%s/%s> ", c1, c2) >= sizeof(pmpt))
			return -1;
	}

	return 0;
}

/*
 * change dbname and/or collname, set ccoll and update prompt.
 * return 0 on success, -1 on failure
 */
int
exec_chcoll(mongoc_client_t * client, const path_t newpath)
{
	/* unset current collection */
	if (ccoll != NULL) {
		mongoc_collection_destroy(ccoll);
		ccoll = NULL;
	}
	/* if there is a new collection, change to it */
	if (strlen(newpath.dbname) && strlen(newpath.dbname))
		ccoll =
		    mongoc_client_get_collection(client, newpath.dbname,
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
 * count number of documents in collection return 0 on success, -1 on failure
 */
int
exec_count(mongoc_collection_t * collection, const char *line, int len)
{
	bson_error_t error;
	bson_t *query;
	int64_t count;

	if (sizeof(tmpdocs) < 3)
		abort();

	if (parse_selector(tmpdocs, sizeof(tmpdocs), line, len) == -1)
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
int
exec_update(mongoc_collection_t * collection, const char *line, int upsert)
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
	offset = parse_selector(tmpdocs, sizeof(tmpdocs), line, strlen(line));
	if (offset <= 0)
		return ILLEGAL;

	line += offset;

	offset = relaxed_to_strict((char *)update_doc, MAXDOC, line, strlen(line), 1);
	if (offset <= 0)
		return ILLEGAL;

	line += offset;

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
			warnx("%d.%d %s", error.domain, error.code, error.message);
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
int
exec_insert(mongoc_collection_t * collection, const char *line, int len)
{
	bson_error_t error;
	bson_t *doc;
	int offset;

	offset = parse_selector(tmpdocs, sizeof(tmpdocs), line, len);
	if (offset <= 0)
		return ILLEGAL;

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
int
exec_remove(mongoc_collection_t * collection, const char *line, int len)
{
	int offset;
	bson_error_t error;
	bson_t *doc;

	offset = parse_selector(tmpdocs, sizeof(tmpdocs), line, len);
	if (offset <= 0)
		return ILLEGAL;

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
 * execute an aggregation pipeline return 0 on success, -1 on failure
 */
int
exec_agquery(mongoc_collection_t * collection, const char *line, int len)
{
	bson_error_t error;
	bson_t *aggr_query;
	mongoc_cursor_t *cursor;
	const bson_t *doc;
	char *str;

	if (sizeof(tmpdocs) < 3)
		abort();

	if (relaxed_to_strict((char *)tmpdocs, sizeof(tmpdocs), line, len, 0)
	    == -1) {
		warnx("could not parse aggregation query document");
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
		return -1;
	}

	mongoc_cursor_destroy(cursor);

	return 0;
}

/*
 * execute command with given arguments return 0 on success, -1 on failure
 */
int
exec_cmd(const int cmd, const char **argv, const char *line, int linelen)
{
	path_t tmppath;

	switch (cmd) {
	case LS:
		return exec_ls(line);
	case DROP:
		return exec_drop(line);
	case ILLEGAL:
		break;
	case CHCOLL:
		/* special case "cd -" */
		if (argv[1][0] == '-' && argv[1][1] == '\0') {
			if (strlcpy(tmppath.dbname, prevpath.dbname, MAXDBNAME)
			    >= MAXDBNAME)
				return -1;
			if (strlcpy(tmppath.collname, prevpath.collname,
			    MAXCOLLNAME) >= MAXCOLLNAME)
				return -1;
		} else {
			if (strlcpy(tmppath.dbname, path.dbname, MAXDBNAME) >=
			    MAXDBNAME)
				return -1;
			if (strlcpy(tmppath.collname, path.collname,
			    MAXCOLLNAME) >= MAXCOLLNAME)
				return -1;
			if (parse_path(argv[1], &tmppath, NULL, NULL) < 0)
				return -1;
		}
		return exec_chcoll(client, tmppath);
	case COUNT:
		return exec_count(ccoll, line, linelen);
	case UPDATE:
		return exec_update(ccoll, line, 0);
	case UPSERT:
		return exec_update(ccoll, line, 1);
	case INSERT:
		return exec_insert(ccoll, line, linelen);
	case REMOVE:
		return exec_remove(ccoll, line, linelen);
	case FIND:
		return exec_query(ccoll, line, linelen, 0);
	case AGQUERY:
		return exec_agquery(ccoll, line, linelen);
	}

	return -1;
}

char *
prompt()
{
	return pmpt;
}

/*
 * set username and home dir return 0 on success or -1 on failure.
 */
int
init_user(user_t * usr)
{
	struct passwd *pw;

	if ((pw = getpwuid(getuid())) == NULL)
		return -1;	/* user not found */
	if (strlcpy(usr->name, pw->pw_name, MAXUSERNAME) >= MAXUSERNAME)
		return -1;	/* username truncated */
	if (strlcpy(usr->home, pw->pw_dir, PATH_MAX) >= PATH_MAX)
		return -1;	/* home dir truncated */

	return 0;
}

/*
 * read the credentials from a users config file return 0 on success or -1 on
 * failure.
 */
int
mv_parse_file(FILE * fp, config_t * cfg)
{
	char line[MAXMONGOURL + 1];

	/* expect url on first line */
	if (fgets(line, sizeof(line), fp) == NULL) {
		if (ferror(fp))
			err(1, "mv_parse_file");
		return 0;	/* empty line */
	}
	/* trim newline if any */
	line[strcspn(line, "\n")] = '\0';

	if (strlcpy(cfg->url, line, MAXMONGOURL) >= MAXMONGOURL)
		return -1;

	return 0;
}

/*
 * Handle special import mode, expect one extended json object per line.
 *
 * Returns the number of inserted objects on success, or -1 on error with errno
 * set.
 */
int
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

/*
 * try to read ~/.mongovi and set cfg return 1 if config is read and set, 0
 * if no config is found or -1 on failure.
 */
int
read_config(user_t * usr, config_t * cfg)
{
	const char *file = ".mongovi";
	char tmppath[PATH_MAX];
	FILE *fp;
	struct stat st;

	if ((size_t)snprintf(tmppath, sizeof(tmppath), "%s/%s", usr->home, file)
	    >= sizeof(tmppath))
		return -1;

	if ((fp = fopen(tmppath, "re")) == NULL) {
		if (errno == ENOENT)
			return 0;

		return -1;
	}
	if (fstat(fileno(fp), &st) < 0)
		err(1, "read_config");

	if (st.st_mode & (S_IROTH | S_IWOTH)) {
		fprintf(stderr,
			"ignoring %s, because it is readable and/or writable by others\n",
			tmppath);
		fclose(fp);
		return 0;
	}
	if (mv_parse_file(fp, cfg) < 0) {
		fclose(fp);
		return -1;
	}
	fclose(fp);
	return 1;
}

void
printversion(int d)
{
	dprintf(d, "%s v%d.%d.%d\n", progname, VERSION_MAJOR,
	    VERSION_MINOR, VERSION_PATCH);
}

void
printusage(int d)
{
	dprintf(d, "usage: %s [-psih] [/database/collection]\n", progname);
}

int
main(int argc, char **argv)
{
	const wchar_t *line;
	const char **av;
	char linecpy[MAXLINE], *lp;
	int i, read, status, ac, cmd, c;
	EditLine *e;
	History *h;
	HistEvent he;
	Tokenizer *t;
	path_t newpath = {"", ""};

	char connect_url[MAXMONGOURL] = "mongodb://localhost:27017";

	setlocale(LC_CTYPE, "");

	assert((MB_CUR_MAX) > 0 && (MB_CUR_MAX) < 8);

	if (strlcpy(progname, basename(argv[0]), MAXPROG) >= MAXPROG)
		errx(1, "program name too long");

	/* default ttys to human readable output */
	if (isatty(STDOUT_FILENO))
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

	if (init_user(&user) < 0)
		errx(1, "can't initialize user");

	if ((status = read_config(&user, &config)) < 0)
		errx(1, "can't read config file");
	else if (status > 0)
		if (strlcpy(connect_url, config.url, MAXMONGOURL) >=
		    MAXMONGOURL)
			errx(1, "url in config too long");
	/* else use default */

	/* setup mongo */
	mongoc_init();
	if ((client = mongoc_client_new(connect_url)) == NULL)
		errx(1, "can't connect to mongo");

	if (argc == 1) {
		if (parse_path(argv[0], &newpath, NULL, NULL) < 0)
			errx(1, "illegal path spec");
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
		mongoc_client_destroy(client);
		mongoc_cleanup();

		exit(0);
	}

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
		if (read > MAXLINE || wcstombs(NULL, line, 0) + 1 > MAXLINE) {
			warnx("MAXLINE too short: %ld bytes needed, have %d", wcstombs(NULL, line, 0) + 1, MAXLINE);
			continue;
		}

		if (read == 0)
			goto done;	/* happens on Ubuntu 12.04 without
					   tty */

		/* make room for a copy */
		linecpy[0] = '\0';

		if (wcstombs(linecpy, line, MAXLINE) == (size_t)-1) {
			warnx("locale wcstombs error");
			continue;
		}

		/* trim newline if any (might error on exotic, non-C and non-UTF8 locales) */
		linecpy[strcspn(linecpy, "\n")] = '\0';

		/* tokenize */
		tok_reset(t);
		if (tok_str(t, linecpy, &ac, &av) != 0) {
			warnx("can't tokenize line");
			continue;
		}

		if (ac == 0)
			continue;

		if (history(h, &he, H_ENTER, linecpy) == -1)
			warnx("can't enter history");

		cmd = mv_parse_cmd(ac, av, linecpy, &lp);
		switch (cmd) {
		case ILLEGAL:
			warnx("illegal syntax");
			continue;
			break;
		case UNKNOWN:
			warnx("unknown command");
			continue;
			break;
		case AMBIGUOUS:
			/* matches more than one command, print list_match */
			i = 0;
			while (list_match[i] != NULL)
				printf("%s\n", list_match[i++]);
			continue;
			break;
		case HELP:
			i = 0;
			while (cmds[i] != NULL)
				printf("%s\n", cmds[i++]);
			continue;
			break;
		case DBMISSING:
			warnx("no database selected");
			continue;
			break;
		case COLLMISSING:
			warnx("no collection selected");
			continue;
			break;
		}

		if (exec_cmd(cmd, av, lp, strlen(lp)) == -1)
			warnx("execution failed");
	}

done:
	if (read == -1)
		err(1, NULL);

	if (ccoll != NULL)
		mongoc_collection_destroy(ccoll);
	mongoc_client_destroy(client);
	mongoc_cleanup();

	tok_end(t);
	history_end(h);
	el_end(e);

	free(list_match);

	if (isatty(STDIN_FILENO))
		printf("\n");

	return 0;
}
