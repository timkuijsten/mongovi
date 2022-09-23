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

#include "compat/compat.h"
#include <stdlib.h>
#include <string.h>

/*
 * Match all strings in "src" that start with "prefix".
 *
 * "src" must be an argv style NULL terminated array with pointers to null
 * terminated strings.
 *
 * On success "matches" contains an argv style NULL terminated list of pointers
 * into "src" that match "prefix". On success "matches" must be free(3)d by the
 * caller.
 *
 * Return 0 on success, -1 on error.
*/
int
prefix_match(char ***matches, const char **src, const char *prefix)
{
	int i, listsize, prefsize;

	prefsize = strlen(prefix);

	listsize = 1;
	if ((*matches = reallocarray(NULL, listsize, sizeof(char **))) == NULL)
		return -1;
	(*matches)[listsize - 1] = NULL;

	if (prefsize == 0)
		return 0;

	/* check all strings that start with the given prefix */
	for (i = 0; src[i] != NULL; i++) {
		if (strncmp(prefix, src[i], prefsize) != 0)
			continue;

		listsize++;
		if ((*matches = reallocarray(*matches, listsize, sizeof(char **))) == NULL) {
			free(*matches);
			return -1;
		}
		(*matches)[listsize - 2] = (char *)src[i];
		(*matches)[listsize - 1] = NULL;
	}

	return 0;
}

/*
   returns the length of the maximum prefix that is common for each entry in
   av excluding any terminating null character, or -1 on error
*/
int
common_prefix(const char **av)
{
	int i, j;
	char c, n;		/* current and next character */

	if (av == NULL || av[0] == NULL)
		return 0;

	i = 0;
	j = 0;
	c = av[i][j];
	n = c;

	while (n == c) {
		if (av[i]) {
			n = av[i][j];
			if (n == 0)
				return j;
			i++;
		} else {
			i = 0;
			j++;
			c = av[i][j];
			n = c;
		}
	}

	return j;
}
