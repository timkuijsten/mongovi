#ifndef COMPAT_H
#define COMPAT_H

#include <sys/types.h>

#ifdef __linux__
size_t strlcpy(char *dst, const char *src, size_t size);
#endif

void *reallocarray(void *optr, size_t nmemb, size_t size);

#endif
