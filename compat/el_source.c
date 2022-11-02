/*      $NetBSD: el.c,v 1.100 2021/08/15 10:08:41 christos Exp $        */

/*-
 * Copyright (c) 1992, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Christos Zoulas of Cornell University.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <histedit.h>

static int
parse_line(EditLine *el, const wchar_t *line)
{
        const wchar_t **argv;
        int argc;
        TokenizerW *tok;

        tok = tok_winit(NULL);
        tok_wstr(tok, line, &argc, &argv);
        argc = el_wparse(el, argc, argv);
        tok_wend(tok);
        return argc;
}

/*
 * Since el_source is broken on Debian 11, we ship a newer version of
 * el_source(3) taken from:
 * commit 297f1a1a86aecbc5c31e17104abfe6a9856d15ce (tag: upstream/3.1-20210522)
 * Author: Sylvestre Ledru <sylvestre@debian.org>
 * Date:   Sat May 29 10:41:16 2021 +0200
 *
 *     New upstream version 3.1-20210522
 *
 * https://salsa.debian.org/debian/libedit.git 
 *
 *
 * That in turn is based on:
 * 2021-05-22 Jess Thrysoee
 *
 *    * version-info: 0:66:0
 *
 *    * all: sync with upstream source
 *
 *    * src/el.c: editrc not read on systems without issetugid
 *      Patch by Trevor Cordes
 *
 * https://thrysoee.dk/editline/
 *
 * With some changes for readability.
 */
int
backup_el_source(EditLine *el)
{
	wchar_t buf[PATH_MAX];
	char path[PATH_MAX];
	const wchar_t *dptr;
	const char *fname;
	char *line;
	FILE *fp;
	size_t n;
	ssize_t r;
	int error = 0;

	fname = getenv("EDITRC");
	if (fname == NULL) {
		fname = getenv("HOME");
		if (fname == NULL)
			return -1;

		if ((size_t)snprintf(path, sizeof(path), "%s%s", fname,
		    "/.editrc") >= sizeof(path)) {
			warnx("editrc path buffer too small: %s", path);
			return -1;
		}

		fname = path;
	}

	if (fname[0] == '\0')
		return -1;

	fp = fopen(fname, "r");
	if (fp == NULL) {
		warn("failed opening: %s", fname);
		return -1;
	}

	line = NULL;
	n = 0;
	while ((r = getline(&line, &n, fp)) != -1) {
		if (r > 0 && line[r - 1] == '\n') {
			line[r - 1] = '\0';
			r--;
		}

		if (r == 0)
			continue;

		n = mbstowcs(buf, line, PATH_MAX);
		if (n == (size_t)-1 || n >= PATH_MAX) {
			warnx("could not decode: \"%s\"", line);
			continue;
		}

		dptr = buf;

		while (*dptr != '\0' && iswspace(*dptr))
			dptr++;

		if (*dptr == '#')
			continue;

		if ((error = parse_line(el, dptr)) == -1)
			break;
	}

	free(line);
	fclose(fp);

	return error;
}
