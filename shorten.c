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

// ensure str is at most maxlen chars
// if str > maxlen, set middle of string to .. and remove chars at both sides of the middle
// str must be \0 terminated and should be at least maxlen + 1 bytes
// maxlen must be >= MINSHORTENED
// return -1 on failure or the size of the new string
int
shorten(char *str, int maxlen)
{
  int half, len, i;

  if (maxlen < MINSHORTENED) // need at least four chars
    return -1;

  len = strlen(str);

  if ((len - maxlen) <= 0)
    return len;

  // len > maxlen >= MINSHORTENED
  // shorten str to maxlen

  // truncate fist half by putting the dots in the middle
  half = maxlen / 2;
  str[half - 1] = '.';
  str[half] = '.';

  // remove characters after the second dot
  for (i = half + 1; i < maxlen; i++)
    str[i] = str[len + i - maxlen];
  str[maxlen] = '\0';

  return maxlen;
}
