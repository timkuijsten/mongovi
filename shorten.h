#ifndef SHORTEN_H
#define SHORTEN_H

#include <sys/types.h>

#define MINSHORTENED 4   /* 4 is min width of a shortened string, see shorten.c */

int shorten(char *str, int maxlen);
int shorten_comps(char *c1, char *c2, int maxlen);

#endif
