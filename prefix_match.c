#include "prefix_match.h"

/*
write a list in dst of all strings in src that start with the given prefix
src must be an argv style null terminated list of null terminated strings
set an argv style NULL terminated list in dst of strings in src with the same prefix
dst should be freed after usage
return 0 on success, -1 on error.
*/
int
prefix_match(char ***dst, const char **src, const char *prefix)
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
    (*dst)[listsize - 2] = (char *)src[i];
    (*dst)[listsize - 1] = NULL;
  }

  return 0;
}
