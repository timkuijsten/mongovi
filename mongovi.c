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

#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <histedit.h>

#define MAXLINE 1024
#define MAXPROMPT 10

static char p[MAXPROMPT];
char *prompt(EditLine *e);
void ferrno(const char *err);
void fatal(const char *err);

int main(int argc, char **argv)
{
  const char *line;
  int on, read;
  EditLine *e;
  History *h;
  HistEvent he;

  // set prompt
  strlcpy(p, basename(argv[0]), sizeof(p));
  strlcat(p, "> ", sizeof(p));

  if ((h = history_init()) == NULL)
    fatal("can't initialize history");
  if ((e = el_init(argv[0], stdin, stdout, stderr)) == NULL)
    fatal("can't initialize editline");

  if (el_set(e, EL_HIST, history, h) == -1)
    fatal("can't set history on");
  if (el_set(e, EL_PROMPT, prompt) == -1)
    fatal("can't set prompt");
  if (el_get(e, EL_EDITMODE, &on) == -1)
    fatal("can't determine mode");

  if (el_source(e, NULL) == -1) {
    fprintf(stderr, "warning: can't read ~/.editrc, using defaults\n");
  }

  history(h, &he, H_SETSIZE, 100);

  while ((line = el_gets(e, &read)) != NULL) {
    if (history(h, &he, H_ENTER, line) == -1)
      fatal("can't enter history");
    printf("l: %s", line);
  }

  if (read == -1)
    ferrno("read line error");

  history_end(h);
  el_end(e);

  return 0;
}

char *prompt(EditLine *e)
{
  return p;
}

void ferrno(const char *err) {
  perror(err);
  exit(1);
}

void fatal(const char *err) {
  fprintf(stderr, "%s\n", err ? err : "");
  exit(1);
}
