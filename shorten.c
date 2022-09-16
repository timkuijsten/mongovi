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

#include "shorten.h"

#include <string.h>
#include <stdio.h>

/**
 * Ensure str is at most maxlen chars. If str > maxlen, replace chars in the
 * middle with "..". The minimum shortened string is thus "x..y" where x and y
 * are the first and last character of str, respectively.
 *
 * str must be \0 terminated and maxlen must be >= 4.
 * return the new length of str on success or -1 on failure.
 */
int
shorten(char *str, int maxlen)
{
	int half, i, len, offset;

	if (maxlen < MINSHORTENED)	/* need at least four chars */
		return -1;

	len = strlen(str);

	offset = len - maxlen;
	if (offset <= 0)
		return len;

	/* len > maxlen >= MINSHORTENED */

	half = maxlen / 2;
	str[half - 1] = '.';
	str[half] = '.';

	for (i = half + 1; i < maxlen; i++)
		str[i] = str[offset + i];
	str[maxlen] = '\0';

	return maxlen;
}

/**
 * Shorten components from left to right until maxlen is satisfied.
 *
 * c1 and c2 must be \0 terminated and maxlen must be >= 2 * 4.
 * return the new total length of all components on success or -1 on failure.
 */
int
shorten_comps(char *c1, char *c2, int maxlen)
{
	int len, nlen, totlen, overflow;
	char *comp[3];
	char **compp;

	comp[0] = c1;
	comp[1] = c2;
	comp[2] = NULL;

	totlen = 0;
	compp = comp;
	while (*compp)
		totlen += strlen(*compp++);

	overflow = totlen - maxlen;
	if (overflow <= 0)
		return totlen;

	compp = comp;
	while (*compp && overflow > 0) {
		len = strlen(*compp);
		nlen = len - overflow;
		if (nlen < MINSHORTENED)
			nlen = MINSHORTENED;	/* never shorten more than
						   MINSHORTENED */

		if ((nlen = shorten(*compp, nlen)) < 0)
			return -1;
		overflow -= len - nlen;
		totlen -= len - nlen;
		compp++;
	}

	return totlen;
}
