#include "prefix_match.h"

/*
write a list in dst of all strings in src that start with the given prefix
src must be an argv style null terminated list of null terminated strings
set an argv style NULL terminated list in dst of strings in src with the same prefix
dst should be freed after usage
return 0 on success, -1 on error.
*/
int
prefix_match(const char ***dst, const char **src, const char *prefix)
{
  int i, j, listsize, prefsize;

  prefsize = strlen(prefix);

  /* init dst with one element pointing to NULL */
  listsize = 1;
  if ((*dst = reallocarray(NULL, listsize, sizeof(char **))) == NULL)
    return -1;
  (*dst)[listsize - 1] = NULL;

  if (prefsize == 0)
    return 0;

  // check all strings that start with the given prefix
  for (i = 0; src[i] != NULL; i++) {
    for (j = 0; j < prefsize; j++) {
      if (src[i][j] == '\0')
        break;
      if (src[i][j] != prefix[j])
        break;
    }
    if (j != prefsize)
      continue;

    // match, allocate a new pointer
    listsize++;
    if ((*dst = reallocarray(*dst, listsize, sizeof(char **))) == NULL) {
      free(*dst);
      return -1;
    }
    (*dst)[listsize - 2] = src[i];
    (*dst)[listsize - 1] = NULL;
  }

  return 0;
}

/* returns the length of the maximum prefix that is common for each entry in av
 * excluding any terminating null character, or -1 on error
 */
int
common_prefix(const char **av)
{
  int i, j;
  char c, n; /* current and next character */

  if (av == NULL || av[0] == NULL)
    return 0;

  i = 0;
  j = 0;
  c = av[i][j];
  n = c;

  while (n == c) {
    if (av[i]) {
      n = av[i][j];
      if (n == 0)
        return j;
      i++;
    } else {
      i = 0;
      j++;
      c = av[i][j];
      n = c;
    }
  }

  return j;
}
