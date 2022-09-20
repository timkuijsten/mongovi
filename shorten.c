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
#define _XOPEN_SOURCE
#endif

#include "shorten.h"

#include <assert.h>
#include <wchar.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MINSHORTCOL 2		/* 2 is minimum number of columns in a
				   shortened string (..) */

/*
 * Determine the number of columns, characters and bytes in the multibyte
 * string s.
 * Returns 0 on success and -1 on failure (i.e. on encoding errors in s).
 * pre: if s is not NULL then it must contain a null byte to signal the end of
 * the string.
 * note: all parameters are optional. bytes, if passed, will be set to the
 * number of bytes processed in s excluding the terminating null byte.
 */
static int
mbslen(const char *s, size_t *columns, size_t *characters, size_t *bytes)
{
	wchar_t wc;
	size_t co, ch, by, *cop, *chp, *byp;
	int n;

	if (columns == NULL) {
		cop = &co;
	} else {
		cop = columns;
	}

	if (characters == NULL) {
		chp = &ch;
	} else {
		chp = characters;
	}

	if (bytes == NULL) {
		byp = &by;
	} else {
		byp = bytes;
	}

	*cop = 0;
	*chp = 0;
	*byp = 0;

	if (s == NULL) {
		return 0;
	}

	mbtowc(NULL, NULL, MB_CUR_MAX);
	*byp = 0;
	while (s[*byp] != '\0') {
		n = mbtowc(&wc, &s[*byp], MB_CUR_MAX);
		if (n < 0) {
			mbtowc(NULL, NULL, MB_CUR_MAX);
			return -1;
		}

		if (n == 0) /* EOS */
			return 0;

		*byp += n;
		*chp += 1;

		n = wcwidth(wc);
		if (n < 0) {
			mbtowc(NULL, NULL, MB_CUR_MAX);
			return -1;
		}

		*cop += n;
	}

	return *byp;
}

/**
 * Make sure str does not exceed maxcolumns by removing characters in the
 * middle of str and replacing them with ".." if needed.
 *
 * Returns the number of columns in str on success (which is <= maxcolumns) or
 * (size_t)-1 on failure.
 *
 * pre: str must be \0 terminated and maxcolumns must be >= 2.
 */
static size_t
shorten(char *str, size_t maxcolumns)
{
	static wchar_t wcs[PATH_MAX];
	static size_t wcwidths[PATH_MAX];
	size_t i, columnsleft, columnsright, charlen, columnlen, charsleft, charsright;
	int n, bdst;

	if (maxcolumns < MINSHORTCOL)	/* need at least two columns */
		return -1;

	charlen = mbstowcs(NULL, str, 0);

	if (charlen == (size_t)-1 || charlen >= PATH_MAX)
		return -1;

	charlen = mbstowcs(wcs, str, charlen);
	if (charlen == (size_t)-1 || charlen >= PATH_MAX)
		return -1;

	columnlen = 0;
	for (i = 0; i < charlen; i++) {
		n = wcwidth(wcs[i]);
		if (n < 0)
			return -1;

		wcwidths[i] = n;

		columnlen += n;
	}

	if (columnlen <= maxcolumns)
		return columnlen;

	/* number of columns per side excluding "." on each side */
	columnsleft = (maxcolumns + 1) / 2 - 1;
	columnsright = maxcolumns / 2 - 1;

	/*
	 * Prefer the last character over an extra first character if the last
	 * character is two columns and the first characters are one column
	 * each.
	 */
	if (columnsleft == 2 && wcwidths[0] == 1 && columnsright == 1 && wcwidths[charlen - 1] == 2) {
		columnsleft--;
		columnsright++;
	}

	/*
	 * Determine the number of characters to read on the left and right of
	 * str based on the column width of each character.
	 */
	columnlen = 0;
	charsleft = 0;
	while (columnsleft > 0) {
		if (columnsleft >= wcwidths[charsleft]) {
			columnlen += wcwidths[charsleft];
			columnsleft -= wcwidths[charsleft];
			charsleft++;
		} else {
			break;
		}
	}

	charsright = 0;
	while (columnsright > 0) {
		if (columnsright >= wcwidths[charlen - 1 - charsright]) {
			columnlen += wcwidths[charlen - 1 - charsright];
			columnsright -= wcwidths[charlen - 1 - charsright];
			charsright++;
		} else {
			break;
		}
	}

	/*
	 * See if we can squeeze in one more character on the left or right.
	 */
	if (columnsright > 0) {
		if (columnsleft + columnsright >= wcwidths[charsleft]) {
			columnlen += wcwidths[charsleft];
			columnsleft = 0;
			columnsright = 0;
			charsleft++;
		}
	}

	if (columnsleft > 0) {
		if (columnsleft + columnsright >= wcwidths[charlen - 1 - charsright]) {
			columnlen += wcwidths[charlen - 1 - charsright];
			columnsleft = 0;
			columnsright = 0;
			charsright++;
		}
	}

	/* fill str with [0..charsleft] + ".." + [charsright..charlen) */
	wctomb(NULL, L'\0');
	bdst = 0;
	for (i = 0; i < charsleft; i++) {
		if (wcwidths[i] > 0) {
			// Since we converted from a multibyte string, assume
			// there is enough room even though it may be less than
			// MB_CUR_MAX.
			n = wctomb(&str[bdst], wcs[i]);
			if (n == -1)
				return -1;

			bdst += n;
		}
	}

	str[bdst] = '.';
	bdst++;

	str[bdst] = '.';
	bdst++;

	wctomb(NULL, L'\0');
	for (i = 0; i < charsright; i++) {
		if (wcwidths[charlen - charsright + i] > 0) {
			// Since we converted from a multibyte string, assume
			// there is enough room even though it may be less than
			// MB_CUR_MAX.
			n = wctomb(&str[bdst], wcs[charlen - charsright + i]);
			if (n == -1)
				return -1;

			bdst += n;
		}
	}

	str[bdst] = '\0';

	return columnlen + 2;
}

/**
 * Shorten c1 and c2 inline if needed and sure maxcolumns is satisfied.
 *
 * pre: c1 and c2 must be \0 terminated and
 *      maxcolumns must be >= 2 * (2 + MINSHORTCOL)
 * return the new total length of all components on success or (size_t)-1 on failure.
 */
size_t
shorten_comps(char *c1, char *c2, size_t maxcolumns)
{
	size_t excess1, excess2, c1columns, c2columns, totcolumns;
	size_t nlen, overflow;
	float cut;

	if (maxcolumns < 2 * (2 + MINSHORTCOL))
		return -1;

	if (mbslen(c1, &c1columns, NULL, NULL) == -1)
		return -1;

	if (mbslen(c2, &c2columns, NULL, NULL) == -1)
		return -1;

	totcolumns = c1columns + c2columns;

	if (totcolumns <= maxcolumns)
		return totcolumns;

	overflow = totcolumns - maxcolumns;

	/*
	 * Distribute overflow over c1 and c2 in proportion, but never shorten
	 * a component more than 2 + MINSHORTCOL.
	 */
	excess1 = 0;
	excess2 = 0;
	if (c1columns > 2 + MINSHORTCOL)
		excess1 = c1columns - (2 + MINSHORTCOL);

	if (c2columns > 2 + MINSHORTCOL)
		excess2 = c2columns - (2 + MINSHORTCOL);

	cut = overflow * ((float)excess1 / (excess1 + excess2));
	if (cut >= 1.0 || excess2 <= excess1) {
		nlen = shorten(c1, c1columns - cut);
		if (nlen == (size_t)-1)
			return -1;

		overflow -= c1columns - nlen;
		totcolumns -= c1columns - nlen;
		c1columns = nlen;

		if (overflow == 0)
			return totcolumns;
	}

	assert(overflow <= excess2);
	nlen = c2columns - overflow;

	nlen = shorten(c2, nlen);
	if (nlen == (size_t)-1)
		return -1;

	totcolumns -= c2columns - nlen;

	return totcolumns;
}
