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

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include <stdlib.h>
#include <string.h>
#include <histedit.h>

#include "parse_path.h"


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
		return -1;

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
				if ((size_t)snprintf(newpath->dbname,
				    sizeof(newpath->dbname), "%s", av[i]) >=
				    sizeof(newpath->dbname))
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
				if ((size_t)snprintf(newpath->collname,
				    sizeof(newpath->collname), "%s", cp) >=
				    sizeof(newpath->collname))
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
				if ((size_t)snprintf(newpath->collname,
				    sizeof(newpath->collname), "%s", cp) >=
				    sizeof(newpath->collname))
					goto cleanuperr;
				cs = cp - path;
				cp += strlen(av[i]) + 1;
				level = LDB;
				/* we're done */
				i = ac;
			}
			break;
		default:
			goto cleanuperr;
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
