#ifndef SHORTEN_H
#define SHORTEN_H

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#include <sys/types.h>

size_t shorten_comps(char *c1, char *c2, size_t maxlen);

#endif
