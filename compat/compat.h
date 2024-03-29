#ifndef COMPAT_H
#define COMPAT_H

#include <sys/types.h>
#include <histedit.h>

size_t strlcpy(char *dst, const char *src, size_t size);

void *reallocarray(void *optr, size_t nmemb, size_t size);
int backup_el_source(EditLine *el);

#endif
