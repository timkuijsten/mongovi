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

#include "mongovi.h"

#define MAXPROMPTCOLUMNS 30	/* The maximum number of columns the prompt may
				   use. Should be at least "/x..y/x..y> " = 4 +
				   4 + 2 * 4 = 16 since x and y can take at
				   most two columns for one character.
				   NOTE: some UTF-8 characters consume 0 or 2
				   columns. */

static char progname[MAXPROG];

static path_t path, prevpath;

/* use as temporary one-time storage while building a query or query results */
static unsigned char tmpdocs[16 * 1024 * 1024];
static unsigned char *tmpdoc = tmpdocs;

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

/* import mode, treat input lines as json documents force insert command */
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

void
usage(void)
{
	printf("usage: %s [-psih] [/database/collection]\n", progname);
	exit(0);
}

int
main_init(int argc, char **argv)
{
	const wchar_t *line;
	const char **av;
	char linecpy[MAXLINE], *lp;
	int i, read, status, ac, cmd, ch;
	EditLine *e;
	History *h;
	HistEvent he;
	Tokenizer *t;
	path_t newpath = {"", ""};

	char connect_url[MAXMONGOURL] = "mongodb://localhost:27017";

	setlocale(LC_CTYPE, "");

	assert((MB_CUR_MAX) > 0 && (MB_CUR_MAX) < 8);

	if (strlcpy(progname, basename(argv[0]), MAXPROG) > MAXPROG)
		errx(1, "program name too long");

	/* default ttys to human readable output */
	if (isatty(STDIN_FILENO))
		hr = 1;

	while ((ch = getopt(argc, argv, "psih")) != -1)
		switch (ch) {
		case 'p':
			hr = 1;
			break;
		case 's':
			hr = 0;
			break;
		case 'i':
			import = 1;
			break;
		case 'h':
		case '?':
			usage();
			break;
		}
	argc -= optind;
	argv += optind;

	if (argc > 1)
		usage();

	if (PATH_MAX < 20)
		errx(1, "can't determine PATH_MAX");

	if (init_user(&user) < 0)
		errx(1, "can't initialize user");

	if ((status = read_config(&user, &config)) < 0)
		errx(1, "can't read config file");
	else if (status > 0)
		if (strlcpy(connect_url, config.url, MAXMONGOURL) > MAXMONGOURL)
			errx(1, "url in config too long");
	/* else use default */

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

	el_set(e, EL_ADDFN, "complete",
	       "Context sensitive argument completion", complete);
	el_set(e, EL_BIND, "\t", "complete", NULL);

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
	/* check import mode */
	if (import)
		if (isatty(STDIN_FILENO))
			errx(1, "import mode can only be used non-interactively");

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

		if (import) {
			if (wcstombs(NULL, line, 0) + 1 + 7 > MAXLINE) {
				warnx("MAXLINE too short: %ld bytes needed, have %d", wcstombs(NULL, line, 0) + 1 + 7, MAXLINE);
				continue;
			}

			linecpy[0] = 'i';
			linecpy[1] = 'n';
			linecpy[2] = 's';
			linecpy[3] = 'e';
			linecpy[4] = 'r';
			linecpy[5] = 't';
			linecpy[6] = ' ';

			if (wcstombs(&linecpy[7], line, MAXLINE - 7) == (size_t)-1) {
				warnx("locale wcstombs error");
				continue;
			}
		} else {
			if (wcstombs(linecpy, line, MAXLINE) == (size_t)-1) {
				warnx("locale wcstombs error");
				continue;
			}
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

/*
 * tab complete command line
 *
 * if empty, print all commands
 * if matches more than one command, print all with matching prefix
 * if matches exactly one command and not complete, complete
 * if command is complete and needs args, look at that
 */
unsigned char
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
	if (strlcpy(cmd, av[0], MAXCMDNAM) > MAXCMDNAM)
		goto cleanup;

	switch (cc) {
	case 0:		/* on command */
		if (complete_cmd(e, cmd, co) < 0)
			goto cleanup;
		ret = CC_REDISPLAY;
		break;
	case 1:		/* on argument, try to complete all commands
				   that support a path parameter */
		if (strcmp(cmd, "cd") == 0 || strcmp(cmd, "ls") == 0
		    || strcmp(cmd, "drop") == 0)
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
	if (prefix_match((const char ***) &list_match, cmds, tok) == -1)
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
 * Tab complete path. relative paths depend on current context.
 *
 * if empty, print all possible arguments
 * if matches more than one component, print all with matching prefix and zip up
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
	if (strlcpy(tmppath.dbname, path.dbname, MAXDBNAME) > MAXDBNAME)
		return -1;
	if (strlcpy(tmppath.collname, path.collname, MAXCOLLNAME) >
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
		if ((strv =
		     mongoc_client_get_database_names_with_opts(client, NULL,
							   &error)) == NULL)
			errx(1, "%d.%d %s", error.domain, error.code, error.message);

		/* check if this matches one or more entries */
		if (prefix_match
		    ((const char ***) &matches, (const char **) strv,
		     tmppath.dbname) == -1)
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

		if ((strv =
		     mongoc_database_get_collection_names_with_opts(db, NULL,
								 &error)) ==
		    NULL)
			errx(1, "%d.%d %s", error.domain, error.code, error.message);

		mongoc_database_destroy(db);

		/* check if this matches one or more entries */
		if (prefix_match
		    ((const char ***) &matches, (const char **) strv,
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

int
exec_ls(const char *npath)
{
	int ret;
	path_t tmppath;
	mongoc_collection_t *ccoll;

	/* copy current context */
	if (strlcpy(tmppath.dbname, path.dbname, MAXDBNAME) > MAXDBNAME)
		return -1;
	if (strlcpy(tmppath.collname, path.collname, MAXCOLLNAME) >
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
	if (strlcpy(tmppath.dbname, path.dbname, MAXDBNAME) > MAXDBNAME)
		return -1;
	if (strlcpy(tmppath.collname, path.collname, MAXCOLLNAME) >
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

/*
 * Create a mongo extended JSON id selector document. If selector is 24 hex
 * digits treat it as an object id, otherwise as a literal.
 *
 * doc     - resulting json doc is set in doc
 * dosize  - the size of doc
 * sel     - selector, does not have to be NUL terminated
 * sellen  - length of sel, excluding a terminating NUL character, if any
 *
 * Return 0 on success or -1 on error.
 */
int
idtosel(char *doc, const size_t docsize, const char *sel, const size_t sellen)
{
	char *idtpls = "{ \"_id\": \"";
	char *idtple = "\" }";
	char *oidtpls = "{ \"_id\": { \"$oid\": \"";
	char *oidtple = "\" } }";
	char *start, *end;

	if (docsize < 1)
		return -1;
	if (sellen < 1)
		return -1;

	/* if 24 hex chars, assume an object id */
	if (sellen == 24 && (strspn(sel, "0123456789abcdefABCDEF") == 24)) {
		start = oidtpls;
		end = oidtple;
	} else {
		/* otherwise treat as a literal */
		start = idtpls;
		end = idtple;
	}

	if (strlen(start) + sellen + strlen(end) + 1 > docsize)
		return -1;

	if (strlcpy(doc, start, docsize) > docsize)
		return -1;
	strncat(doc, sel, sellen);
	doc[strlen(start) + sellen] = '\0';	/* ensure NUL termination */
	if (strlcat(doc, end, docsize) > docsize)
		return -1;

	return 0;
}

/*
 * parse json docs or id only specifications
 * return size of parsed length on success or -1 on failure.
 */
int
parse_selector(unsigned char *doc, const size_t docsize, const char *line,
    int len)
{
	int offset;

	/* support id only selectors */
	const char *ids;	/* id start */
	size_t fnb, snb;	/* first and second non-blank characters used
				   for id selection */

	offset = 0;

	/*
         * if first non-blank char is not a "{", use it as a literal and convert
         * to an id selector
         */
	fnb = strspn(line, " \t");
	if (line[fnb] != '{') {
		ids = line + fnb;	/* id start */
		snb = strcspn(ids, " \t");	/* id end */

		idtosel((char *) doc, docsize, ids, snb);
		offset = fnb + snb;
	} else {
		/* try to parse as relaxed json and convert to strict json */
		if ((offset = relaxed_to_strict(doc, docsize, line, len, 1)) < 0) {
			warnx("jsonify error: %d", offset);
			return -1;
		}
	}

	return offset;
}

/*
 * Parse path that consists of a database name and or a collection name. Support
 * both absolute and relative paths.
 * Absolute paths always start with a / followed by a database name.
 * Relative paths depend on the db and collection values in newpath.
 * paths must be null terminated.
 * ".." is supported as a way to go up, but only if it does not follow a
 * collection name, since "/" and ".." are valid characters for a collection and
 * are thus treated as part of that collection name.
 *
 * if dbstart is not NULL the byte index is set to the start of the database
 * component
 * if collstart is not NULL the byte index is set to the start of the
 * collection component both are -1 if not in paths
 *
 * Return 0 on success, -1 on failure.
 */
int
parse_path(const char *paths, path_t *newpath, int *dbstart, int *collstart)
{
	enum levels {
		LNONE, LDB, LCOLL
	};
	int i, ac, ds, cs;
	enum levels level;
	const char **av;
	Tokenizer *t;
	char *path, *cp;

	ds = -1;		/* dbstart index */
	cs = -1;		/* collstart index */

	/* init indices on request */
	if (dbstart != NULL)
		*dbstart = ds;

	if (collstart != NULL)
		*collstart = cs;

	i = strlen(paths);
	if (!i)
		return 0;

	if ((path = strdup(paths)) == NULL)
		err(1, "parse_path");

	cp = path;

	/* trim trailing blanks */
	while (cp[i - 1] == ' ' || cp[i - 1] == '\t' || cp[i - 1] == '\n')
		cp[--i] = '\0';

	/* trim leading blanks */
	while (*cp == ' ' || *cp == '\t' || *cp == '\n')
		cp++;

	/* before we start parsing, determine current depth level */
	if (cp[0] == '/') {	/* absolute path, reset db and collection */
		level = LNONE;	/* not in db or collection */
		newpath->dbname[0] = '\0';
		newpath->collname[0] = '\0';
	} else {		/* relative path */
		if (strlen(newpath->collname))
			level = LCOLL;	/* in collection (and thus db) */
		else if (strlen(newpath->dbname))
			level = LDB;	/* in db */
		else
			level = LNONE;	/* no db or collection set */
	}

	t = tok_init("/");
	tok_str(t, cp, &ac, &av);

	/* now start parsing cp */
	i = 0;
	if (cp[0] == '/')
		cp++;

	while (i < ac) {
		switch (level) {
		case LNONE:
			if (strcmp(av[i], "..") == 0) {	/* skip */
				cp += 2 + 1;
			} else {
				/* use component as database name */
				if (strlcpy(newpath->dbname, av[i], MAXDBNAME) > MAXDBNAME)
					goto cleanuperr;
				ds = cp - path;
				cp += strlen(av[i]) + 1;
				level = LDB;
			}
			break;
		case LDB:
			if (strcmp(av[i], "..") == 0) {	/* go up */
				newpath->dbname[0] = '\0';
				cp += 2 + 1;
				ds = -1;
				level = LNONE;
			} else {
				/*
				 * use all remaining tokens as the name of
				 * the collection:
				 */
				if ((strlcpy(newpath->collname, cp, MAXCOLLNAME)) >
				    MAXCOLLNAME)
					goto cleanuperr;
				cs = cp - path;
				cp += strlen(av[i]) + 1;
				/* we're done */
				i = ac;
			}
			break;
		case LCOLL:
			if (strcmp(av[i], "..") == 0) {	/* go up */
				newpath->collname[0] = '\0';
				cp += 2 + 1;
				cs = -1;
				level = LDB;
			} else {
				/*
				 * use all remaining tokens as the name of
				 * the collection:
				 */
				if ((strlcpy(newpath->collname, cp, MAXCOLLNAME)) >
				    MAXCOLLNAME)
					goto cleanuperr;
				cs = cp - path;
				cp += strlen(av[i]) + 1;
				level = LDB;
				/* we're done */
				i = ac;
			}
			break;
		default:
			errx(1, "unexpected level %d while parsing \"%s\"", level, cp);
		}
		i++;
	}

	/* update indices on request */
	if (dbstart != NULL)
		*dbstart = ds;

	if (collstart != NULL)
		*collstart = cs;

	tok_end(t);
	free(path);

	return 0;

cleanuperr:
	tok_end(t);
	free(path);

	return -1;
}

/* return command code */
int
mv_parse_cmd(int argc, const char *argv[], const char *line, char **lp)
{
	const char *cmd;

	/* check if the first token matches one or more commands */
	if (prefix_match((const char ***) &list_match, cmds, argv[0]) == -1)
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
			if (strlcpy(tmppath.dbname, prevpath.dbname, MAXDBNAME) >
			    MAXDBNAME)
				return -1;
			if (strlcpy(tmppath.collname, prevpath.collname, MAXCOLLNAME) >
			    MAXCOLLNAME)
				return -1;
		} else {
			if (strlcpy(tmppath.dbname, path.dbname, MAXDBNAME) >
			    MAXDBNAME)
				return -1;
			if (strlcpy(tmppath.collname, path.collname, MAXCOLLNAME) >
			    MAXCOLLNAME)
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

	if ((strv =
	     mongoc_client_get_database_names_with_opts(client, NULL,
							&error)) == NULL) {
		warnx("cursor failed: %d.%d %s", error.domain, error.code,
		      error.message);
		return -1;
	}
	for (i = 0; strv[i]; i++)
		if (prefix == NULL) {
			printf("%s\n", strv[i]);
		} else {
			if (strncmp(prefix, strv[i], prefixlen) == 0)
				printf("%s\n", strv[i]);
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
	if (strlcpy(prevpath.dbname, path.dbname, MAXDBNAME) > MAXDBNAME)
		return -1;
	if (strlcpy(prevpath.collname, path.collname, MAXCOLLNAME) >
	    MAXCOLLNAME)
		return -1;
	if (strlcpy(path.dbname, newpath.dbname, MAXDBNAME) > MAXDBNAME)
		return -1;
	if (strlcpy(path.collname, newpath.collname, MAXCOLLNAME) >
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
	int64_t count;
	bson_t *query;

	if (sizeof(tmpdocs) < 3)
		errx(1, "exec_count");
	/* default to all documents */
	tmpdoc[0] = '{';
	tmpdoc[1] = '}';
	tmpdoc[2] = '\0';

	if (parse_selector(tmpdoc, sizeof(tmpdocs), line, len) == -1)
		return -1;

	/* try to parse it as json and convert to bson */
	if ((query = bson_new_from_json(tmpdoc, -1, &error)) == NULL) {
		warnx("%d.%d %s", error.domain, error.code, error.message);
		return -1;
	}
	if ((count =
	     mongoc_collection_count_documents(collection, query, NULL, NULL,
					       NULL, &error)) == -1) {
		warnx("cursor failed: %d.%d %s", error.domain, error.code,
		      error.message);
		bson_destroy(query);
		return -1;
	}
	printf("%ld\n", count);

	bson_destroy(query);

	return 0;
}

/*
 * parse update command, expect two json objects, a selector, and an update
 * doc and exec
 */
int
exec_update(mongoc_collection_t * collection, const char *line, int upsert)
{
	int offset;
	unsigned char update_docs[MAXDOC];
	unsigned char *update_doc = update_docs;
	bson_error_t error;
	bson_t *query, *update;

	int opts = MONGOC_UPDATE_NONE;

	if (upsert)
		opts |= MONGOC_UPDATE_UPSERT;

	/* read first json object */
	if ((offset =
	     parse_selector(tmpdoc, sizeof(tmpdocs), line,
			    strlen(line))) == -1)
		return ILLEGAL;
	if (offset == 0)
		return ILLEGAL;

	/* shorten line */
	line += offset;

	/* read second json object */
	if ((offset =
	     relaxed_to_strict(update_doc, MAXDOC, line, strlen(line),
			       1)) < 0) {
		warnx("jsonify error: %d", offset);
		return ILLEGAL;
	}
	if (offset == 0)
		return ILLEGAL;

	/* shorten line */
	line += offset;

	/* try to parse the query as json and convert to bson */
	if ((query = bson_new_from_json(tmpdoc, -1, &error)) == NULL) {
		warnx("%d.%d %s", error.domain, error.code, error.message);
		return -1;
	}
	/* try to parse the update as json and convert to bson */
	if ((update = bson_new_from_json(update_doc, -1, &error)) == NULL) {
		warnx("%d.%d %s", error.domain, error.code, error.message);
		bson_destroy(query);
		return -1;
	}
	/*
         * execute update, always try with multi first, and if that fails,
         * without
         */
	if (!mongoc_collection_update
	    (collection, opts | MONGOC_UPDATE_MULTI_UPDATE, query, update,
	     NULL, &error)) {
		/*
		 * if error is "multi update only works with $ operators", retry
		 * without MULTI
		 */
		if (error.domain == MONGOC_ERROR_COMMAND
		    && error.code == MONGOC_ERROR_CLIENT_TOO_SMALL) {
			if (!mongoc_collection_update
			  (collection, opts, query, update, NULL, &error)) {
				warnx("%d.%d %s", error.domain, error.code, error.message);
				bson_destroy(query);
				bson_destroy(update);
				return -1;
			}
		} else {
			warnx("%d.%d %s", error.domain, error.code, error.message);
			bson_destroy(query);
			bson_destroy(update);
			return -1;
		}
	}
	bson_destroy(query);
	bson_destroy(update);

	return 0;
}

/* parse insert command, expect one json objects, the insert doc and exec */
int
exec_insert(mongoc_collection_t * collection, const char *line, int len)
{
	int offset;
	bson_error_t error;
	bson_t *doc;

	/* read first json object */
	if ((offset =
	     parse_selector(tmpdoc, sizeof(tmpdocs), line, len)) == -1)
		return ILLEGAL;
	if (offset == 0)
		return ILLEGAL;

	/* try to parse the doc as json and convert to bson */
	if ((doc = bson_new_from_json(tmpdoc, -1, &error)) == NULL) {
		warnx("%d.%d %s", error.domain, error.code, error.message);
		return -1;
	}
	/* execute insert */
	if (!mongoc_collection_insert
	    (collection, MONGOC_INSERT_NONE, doc, NULL, &error)) {
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

	/* read first json object */
	if ((offset =
	     parse_selector(tmpdoc, sizeof(tmpdocs), line, len)) == -1)
		return ILLEGAL;
	if (offset == 0)
		return ILLEGAL;

	/* try to parse the doc as json and convert to bson */
	if ((doc = bson_new_from_json(tmpdoc, -1, &error)) == NULL) {
		warnx("%d.%d %s", error.domain, error.code, error.message);
		return -1;
	}
	/* execute remove */
	if (!mongoc_collection_remove
	    (collection, MONGOC_REMOVE_NONE, doc, NULL, &error)) {
		warnx("%d.%d %s", error.domain, error.code, error.message);
		bson_destroy(doc);
		return -1;
	}
	bson_destroy(doc);

	return 0;
}

/*
 * execute a query return 0 on success, -1 on failure
 */
int
exec_query(mongoc_collection_t * collection, const char *line, int len,
   int idsonly)
{
	int i;
	mongoc_cursor_t *cursor;
	bson_error_t error;
	size_t rlen;
	const bson_t *doc;
	char *str;
	bson_t *query, *fields;
	struct winsize w;

	if (sizeof(tmpdocs) < 3)
		errx(1, "exec_query");
	/* default to all documents */
	tmpdoc[0] = '{';
	tmpdoc[1] = '}';
	tmpdoc[2] = '\0';

	if (parse_selector(tmpdoc, sizeof(tmpdocs), line, len) == -1)
		return -1;

	/* try to parse it as json and convert to bson */
	if ((query = bson_new_from_json(tmpdoc, -1, &error)) == NULL) {
		warnx("%d.%d %s", error.domain, error.code, error.message);
		return -1;
	}
	if (idsonly)
		if ((fields = bson_new_from_json((unsigned char *)
				    "{ \"projection\": { \"_id\": true } }",
						 -1, &error)) == NULL) {
			warnx("%d.%d %s", error.domain, error.code, error.message);
			bson_destroy(query);
			return -1;
		}
	cursor =
	    mongoc_collection_find_with_opts(collection, query,
					     idsonly ? fields : NULL, NULL);

	ioctl(0, TIOCGWINSZ, &w);

	while (mongoc_cursor_next(cursor, &doc)) {
		str = bson_as_json(doc, &rlen);
		if (hr && rlen > w.ws_col) {
			if ((i =
			     human_readable(tmpdoc, sizeof(tmpdocs), str, rlen)) < 0) {
				warnx("jsonify error: %d", i);
				bson_destroy(query);
				if (idsonly)
					bson_destroy(fields);
				return -1;
			}
			printf("%s\n", tmpdoc);
		} else {
			printf("%s\n", str);
		}
		bson_free(str);
	}

	if (mongoc_cursor_error(cursor, &error)) {
		warnx("cursor failed: %d.%d %s", error.domain, error.code,
		      error.message);
		mongoc_cursor_destroy(cursor);
		bson_destroy(query);
		if (idsonly)
			bson_destroy(fields);
		return -1;
	}
	mongoc_cursor_destroy(cursor);

	bson_destroy(query);
	if (idsonly)
		bson_destroy(fields);

	return 0;
}

/*
 * execute an aggregation pipeline return 0 on success, -1 on failure
 */
int
exec_agquery(mongoc_collection_t * collection, const char *line, int len)
{
	int i;
	mongoc_cursor_t *cursor;
	bson_error_t error;
	const bson_t *doc;
	char *str;
	bson_t *aggr_query;

	/* try to parse as relaxed json and convert to strict json */
	if ((i = relaxed_to_strict(tmpdoc, sizeof(tmpdocs), line, len, 0)) < 0) {
		warnx("jsonify error: %d", i);
		return -1;
	}
	/* try to parse it as json and convert to bson */
	if ((aggr_query = bson_new_from_json(tmpdoc, -1, &error)) == NULL) {
		warnx("%d.%d %s", error.domain, error.code, error.message);
		return -1;
	}
	cursor =
	    mongoc_collection_aggregate(collection, MONGOC_QUERY_NONE,
					aggr_query, NULL, NULL);

	while (mongoc_cursor_next(cursor, &doc)) {
		str = bson_as_json(doc, NULL);
		printf("%s\n", str);
		bson_free(str);
	}

	if (mongoc_cursor_error(cursor, &error)) {
		warnx("cursor failed: %d.%d %s", error.domain, error.code,
		      error.message);
		mongoc_cursor_destroy(cursor);
		bson_destroy(aggr_query);
		return -1;
	}
	mongoc_cursor_destroy(cursor);

	bson_destroy(aggr_query);

	return 0;
}

char *
prompt()
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
 * try to read ~/.mongovi and set cfg return 1 if config is read and set, 0
 * if no config is found or -1 on failure.
 */
int
read_config(user_t * usr, config_t * cfg)
{
	const char *file = ".mongovi";
	char tmppath[PATH_MAX + 1];
	FILE *fp;
	struct stat st;

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

	if (strlcpy(cfg->url, line, MAXMONGOURL) > MAXMONGOURL)
		return -1;

	return 0;
}
