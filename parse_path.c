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

#include <stdlib.h>
#include <string.h>
#include <histedit.h>

#include "parse_path.h"

/*
 * Modify n into an absolute path and write the results in c. "." and ".." will
 * be resolved and extraneous slashes will be removed.
 *
 * Note that if n is absolute then c is ignored, otherwise c must be a null
 * terminated absolute path. csize must be the total number of bytes available
 * in c and must be >= 2. c and n may point to the same address.
 *
 * If comps is not NULL it will be updated to contain the number of resulting
 * components in c.
 *
 * Returns the new length of c on success excluding the terminating null (which
 * is always >= 1) or (size_t)-1 on failure. If the returned value is >= csize
 * there was not enough space in c to write the terminating null byte.
 */
size_t
resolvepath(char *c, size_t csize, const char *n, int *comps)
{
	char *cp;
	size_t i, j;
	int comp;

	if (csize <= 1)
		return (size_t)-1;

	if (comps == NULL)
		comps = &comp;

	*comps = 0;

	/*
	 * cp invariant:
	 * Let cp point to the position where a next "/comp" should be written.
	 * Points to the slash in the case of a root only, else one position
	 * after the last character of the last component (which should be
	 * '\0').
	 */

	cp = c;

	if (*n == '/') {
		cp[0] = '/';
		n++;
	} else {
		if (c[0] != '/')
			return (size_t)-1;

		i = resolvepath(c, csize, c, comps);
		if (i == (size_t)-1 || i >= csize)
			return i;

		cp = &c[i - 1];

		/* maintain cp invariant */
		if (i > 1)
			cp++;
	}

	for(;;) {
		j = strcspn(n, "/");

		if (j == 1 && n[0] == '.') {
			n += 1;
			continue;
		} else if (j == 2 && n[0] == '.' && n[1] == '.') {
			while (*cp != '/')
				cp--;

			if (cp > c)
				*cp = '\0';

			n += 2;
			if (*comps > 0)
				*comps -= 1;
			continue;
		}

		if (j > 0) {
			/* append */
			if (csize - (cp - c) < j + 2)
				return (cp - c) + j + 1;

			*cp = '/';
			cp++;

			for (i = 0; i < j; i++)
				cp[i] = n[i];

			cp += j;
			n += j;
			*comps += 1;
		}

		j = strspn(n, "/");
		if (j == 0) {
			if (cp == c)
				cp++;

			*cp = '\0';
			return cp - c;
		}

		n += j;

		/* support c == n so terminate cp after n is increased */
		if (cp > c)
			*cp = '\0';
	}
}

/*
 * Parse an absolute path that may consist of a database and a collection name.
 *
 * Return 0 on success, -1 on failure.
 */
int
parse_path(path_t *p, const char *path)
{
	int i;
	char *coll;

	if (*path != '/')
		return -1;

	p->dbname[0] = '\0';
	p->collname[0] = '\0';

	coll = strchr(path + 1, '/');
	if (coll == NULL) {
		i = snprintf(p->dbname, sizeof(p->dbname), "%s", path + 1);
		if ((size_t)i >= sizeof(p->dbname))
			return -1;
	} else {
		i = snprintf(p->dbname, sizeof(p->dbname), "%.*s",
		    (int)(coll - (path + 1)), path + 1);
		if ((size_t)i >= sizeof(p->dbname))
			return -1;

		i = snprintf(p->collname, sizeof(p->collname), "%s", coll + 1);
		if ((size_t)i >= sizeof(p->collname))
			return -1;
	}

	return 0;
}
