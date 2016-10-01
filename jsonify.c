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

#include "jsonify.h"

static int sp = 0;
static int stack[MAXSTACK];
static char closesym[MAXSTACK];

static char out[MAXOUTPUT];
static size_t outsize = MAXOUTPUT;

long
relaxed_to_strict(char *dst, size_t dstsize, const char *src, size_t srcsize, int firstonly)
{
  size_t i;
  ssize_t nrtokens;
  jsmn_parser parser;
  jsmntok_t tokens[TOKENS];

  if (srcsize > LONG_MAX)
    return -1;

  if (firstonly) {
    // stop after first document (root)
    i = 0;
    do {
      jsmn_init(&parser);
      nrtokens = jsmn_parse(&parser, src, i, tokens, TOKENS);
    } while (i++ < srcsize && (nrtokens == JSMN_ERROR_PART || nrtokens == 0));
    i--;
  } else {
    jsmn_init(&parser);
    i = srcsize;
    nrtokens = jsmn_parse(&parser, src, srcsize, tokens, TOKENS);
  }

  if (nrtokens == 0)
    return 0;
  else if (nrtokens < 0)
    return -1;

  // wipe internal buffer
  out[0] = '\0';
  if (iterate(src, tokens, nrtokens, (void (*)(jsmntok_t *, char *, int, int, char *))writer) == -1)
    return -1;

  if (strlcpy(dst, out, dstsize) > dstsize)
    return -1;

  return i;
}

int
iterate(const char *src, jsmntok_t *tokens, int nrtokens, void (*iterator)(jsmntok_t *, char *, int, int, char *))
{
  char *key, *cp, c;
  jsmntok_t *tok;
  int i, j;
  int depth, ndepth;

  depth = ndepth = 0;

  for (i = 0; i < nrtokens; i++) {
    tok = &tokens[i];
    key = strndup(src + tok->start, tok->end - tok->start);

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

    iterator(tok, key, depth, ndepth, closesym);
    depth = ndepth;
  }

  return 0;
}

void
writer(jsmntok_t *tok, char *key, int depth, int ndepth, char *closesym)
{
  switch (tok->type) {
  case JSMN_OBJECT:
    strlcat(out, "{", outsize);
    break;
  case JSMN_ARRAY:
    strlcat(out, "[", outsize);
    break;
  case JSMN_UNDEFINED:
    if (tok->size) { // quote keys
      strlcat(out, "\"undefined\":", outsize);
    } else { // don't quote values
      strlcat(out, key, outsize);
    }
    break;
  case JSMN_STRING:
    strlcat(out, "\"", outsize);
    strlcat(out, key, outsize);
    strlcat(out, "\"", outsize);
    if (tok->size) // this is a key
      strlcat(out, ":", outsize);
    break;
  case JSMN_PRIMITIVE:
    // convert single quotes at beginning and end of string
    if (key[0] == '\'')
      key[0] = '"';
    if (key[strlen(key) - 1] == '\'')
      key[strlen(key) - 1] = '"';

    if (tok->size) { // quote keys
      strlcat(out, "\"", outsize);
      strlcat(out, key, outsize);
      strlcat(out, "\":", outsize);
    } else // don't quote values
      strlcat(out, key, outsize);
    break;
  default:
    warnx("unknown json token type");
  }

  // write any closing symbols
  if (strlcat(out, closesym, outsize) > outsize)
    return;

  // if not increasing and not heading to the end of this root
  if (ndepth && depth >= ndepth)
    if (!tok->size) // and if not a key
      if (strlcat(out, ",", outsize) > outsize)
        return;
}

// pop item from the stack
// return item on the stack on success, -1 on error
int pop()
{
  if (sp == 0)
    return -1;
  return stack[--sp];
}

// push new item on the stack
// return 0 on success, -1 on error
int push(int val)
{
  if (val == -1) // don't support -1 values, reserved for errors
    return -1;
  if (sp == MAXSTACK)
    return -1;
  stack[sp++] = val;
  return 0;
}
