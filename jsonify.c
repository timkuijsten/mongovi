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

#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jsmn.h"

#define TOKENS 100000
#define MAXSTACK 10000

static int sp = 0;
static int stack[MAXSTACK];
static char closesym[MAXSTACK];

static char *out;
static size_t outsize;
static size_t outidx = 0;

/* pop item from the stack */
/* return item on the stack on success, -1 on error */
static int
pop()
{
	if (sp == 0)
		return -1;
	return stack[--sp];
}

/* push new item on the stack */
/* return 0 on success, -1 on error */
static int
push(int val)
{
	if (val == -1)		/* don't support -1 values, reserved for
				   errors */
		return -1;
	if (sp == MAXSTACK)
		return -1;
	stack[sp++] = val;
	return 0;
}

static int
addout(char *src, size_t size)
{
	if (outidx + size >= outsize)
		return -1;
	memcpy(out + outidx, src, size);
	outidx += size;
	out[outidx] = '\0';
	return 0;
}

/*
 * Run iterator on each token in tokens.
 *
 * If maxroots > 0, then at most this many root tokens (and it's children) will
 * be parsed.
 *
 * Returns the index in tokens of the last parsed root token on success or -1
 * on failure.
 */
static int
iterate(const char *src, jsmntok_t * tokens, int nrtokens, int maxroots,
    int (*iterator)(jsmntok_t *, char *, int, int, char *))
{
	char *key, *cp, c;
	jsmntok_t *tok;
	int i, j;
	int depth, ndepth, roottokens, lastroottoken;

	depth = 0;
	ndepth = 0;
	roottokens = 0;
	lastroottoken = -1;

	for (i = 0; i < nrtokens; i++) {
		// consider each token at depth 0 a root
		if (depth == 0) {
			if (maxroots > 0 && maxroots == roottokens) {
				return lastroottoken;
			}

			roottokens++;
			lastroottoken = i;
		}

		tok = &tokens[i];
		key = strndup(src + tok->start, tok->end - tok->start);
		if (key == NULL)
			err(1, NULL);

		switch (tok->type) {
		case JSMN_OBJECT:
			push('}');
			ndepth++;
			for (j = 0; j < tok->size - 1; j++)
				if (push(',') == -1)
					warnx("stack push error");
			break;
		case JSMN_ARRAY:
			push(']');
			ndepth++;
			for (j = 0; j < tok->size - 1; j++)
				if (push(',') == -1)
					warnx("stack push error");
			break;
		case JSMN_UNDEFINED:
		case JSMN_STRING:
		case JSMN_PRIMITIVE:
			break;
		}

		cp = closesym;
		if (!tok->size) {
			while ((c = pop()) == ']' || c == '}') {
				ndepth--;
				*cp++ = c;
			}
		}
		*cp = '\0';

		if (outidx < outsize) {
			if (iterator(tok, key, depth, ndepth, closesym) < 0) {
				free(key);
				return -1;
			}
		} else {
			free(key);
			return -1;
		}

		depth = ndepth;

		free(key);
	}

	return lastroottoken;
}

/*
 * Convert json objects to contain two spaces per indent level and newlines
 * after every key value pair or opening of a new object. Also sprinkle in some
 * white space in arrays and don't quote keys.
 */
static int
human_readable_writer(jsmntok_t * tok, char *key, int depth, int ndepth,
    char *closesym)
{
	size_t i;
	int j;

	switch (tok->type) {
	case JSMN_OBJECT:
		addout("{", 1);
		break;
	case JSMN_ARRAY:
		addout("[", 1);
		break;
	case JSMN_STRING:
		if (tok->size) {/* this is a key */
			addout("\n", 1);
			/* indent with two spaces per next depth */
			for (i = 0; i < (size_t) ndepth; i++)
				addout("  ", 2);
			addout(key, strlen(key));
			addout(": ", 2);
		} else {	/* this is a value */
			addout("\"", 1);
			addout(key, strlen(key));
			addout("\"", 1);
		}
		break;
	case JSMN_UNDEFINED:
	case JSMN_PRIMITIVE:
		if (tok->size) {/* this is a key */
			addout("\n", 1);
			/* indent with two spaces per next depth */
			for (i = 0; i < (size_t) ndepth; i++)
				addout("  ", 2);
			addout(key, strlen(key));
			addout(": ", 2);
		} else {	/* this is a value */
			addout(key, strlen(key));
		}
		break;
	default:
		warnx("unknown json token type");
	}

	for (i = 0; i < strlen(closesym); i++) {
		/* indent with two spaces per depth */
		if (closesym[i] == '}') {
			if (ndepth < depth)
				if (addout("\n", 1) < 0)
					return -1;
			for (j = 1; (size_t) j < depth - i; j++)
				addout("  ", 2);

			if (addout("}", 1) < 0)
				return -1;
		} else if (closesym[i] == ']') {
			if (addout("]", 1) < 0)
				return -1;
		} else {
			/* unknown character */
			return -1;
		}
	}

	/* if not increasing and not heading to the end of this root */
	if (ndepth && depth >= ndepth)
		if (!tok->size)	/* and if not a key */
			if (addout(",", 1) < 0)
				return -1;

	return 0;
}

static int
strict_writer(jsmntok_t * tok, char *key, int depth, int ndepth, char *closesym)
{
	size_t keylen;

	switch (tok->type) {
	case JSMN_OBJECT:
		addout("{", 1);
		break;
	case JSMN_ARRAY:
		addout("[", 1);
		break;
	case JSMN_UNDEFINED:
		if (tok->size) {/* quote keys */
			addout("\"undefined\":", 11);
		} else {	/* don't quote values */
			addout(key, strlen(key));
		}
		break;
	case JSMN_STRING:
		keylen = strlen(key);
		addout("\"", 1);
		addout(key, keylen);
		addout("\"", 1);
		if (tok->size)	/* this is a key */
			addout(":", 1);
		break;
	case JSMN_PRIMITIVE:
		keylen = strlen(key);
		/* convert single quotes at beginning and end of string */
		if (key[0] == '\'')
			key[0] = '"';
		if (key[keylen - 1] == '\'')
			key[keylen - 1] = '"';

		if (tok->size) {/* quote keys */
			addout("\"", 1);
			addout(key, keylen);
			addout("\":", 2);
		} else		/* don't quote values */
			addout(key, keylen);
		break;
	default:
		warnx("unknown json token type");
	}

	/* write any closing symbols */
	if (addout(closesym, strlen(closesym)) < 0)
		return -1;

	/* if not increasing and not heading to the end of this root */
	if (ndepth && depth >= ndepth)
		if (!tok->size)	/* and if not a key */
			if (addout(",", 1) < 0)
				return -1;

	return 0;
}

/*
static void
print_tokens(const char *src, jsmntok_t *tokens, int nrtokens)
{
	int i;

	for (i = 0; i < nrtokens; i++)
		printf("%2d T%d, s: %2d, e: %2d, nest: %d \"%.*s\"\n",
		    i,
		    tokens[i].type,
		    tokens[i].start,
		    tokens[i].end,
		    tokens[i].size,
		    tokens[i].end - tokens[i].start,
		    &src[tokens[i].start]);
}
*/

/*
 * Create an indented representation of src with keys unescaped.
 *
 * Returns the number of bytes parsed in src on success, or -1 on error.
 * On success, if dstsize > 0 a null byte is always written.
 */
int
human_readable(char *dst, size_t dstsize, const char *src, size_t srcsize)
{
	jsmntok_t tokens[TOKENS];
	jsmn_parser parser;
	ssize_t nrtokens;
	int r;

	jsmn_init(&parser);
	nrtokens = jsmn_parse(&parser, src, srcsize, tokens, TOKENS);

	if (nrtokens < 0)
		return -1;

	if (nrtokens == 0) {
		if (dstsize > 0)
			dst[0] = '\0';

		return 0;
	}

	if (dstsize < 1)
		return -1;

	out = dst;
	outsize = dstsize;
	out[0] = '\0';
	outidx = 0;

	r = iterate(src, tokens, nrtokens, 0, human_readable_writer);
	if (r == -1)
		return -1;

	// return end of last processed root object token
	return tokens[r].end + 1;
}

/*
 * Add double quotes to keys that are unquoted by copying src into dst.
 *
 * Returns the number of bytes parsed in src on success, or -1 on error.
 * On success, if dstsize > 0 a null byte is always written.
 */
int
relaxed_to_strict(char *dst, size_t dstsize, const char *src, size_t srcsize,
    int maxobjects)
{
	jsmntok_t tokens[TOKENS];
	jsmn_parser parser;
	ssize_t nrtokens;
	int r;

	jsmn_init(&parser);
	nrtokens = jsmn_parse(&parser, src, srcsize, tokens, TOKENS);

	if (nrtokens < 0)
		return -1;

	if (nrtokens == 0) {
		if (dstsize > 0)
			dst[0] = '\0';

		return 0;
	}

	if (dstsize < 1)
		return -1;

	out = dst;
	outsize = dstsize;
	out[0] = '\0';
	outidx = 0;

	r = iterate(src, tokens, nrtokens, maxobjects, strict_writer);
	if (r == -1)
		return -1;

	// return end of last processed root object token
	return tokens[r].end + 1;
}
