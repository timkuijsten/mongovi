#ifndef COMPAT_H
#define COMPAT_H

#ifdef __linux__
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);
#endif

#endif

