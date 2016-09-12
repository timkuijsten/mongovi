#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

void fstrerror()
{
  ferrno(strerror(errno));
}

void ferrno(const char *err)
{
  perror(err);
  exit(1);
}

void fatal(const char *err)
{
  fprintf(stderr, "%s\n", err ? err : "");
  exit(1);
}
