#include "../prefix_match.h"

#include <err.h>
#include <stdio.h>
#include <string.h>

int test_prefix_match(const char **src, const char *prefix, const char **exp, const int exp_exit);
int arrcmp(const char **a1, const char **a2);

int main()
{
  int failed = 0;
  const char *src[] = {
    "a",
    "b1",
    "b2",
    "b2a",
    "c",
    NULL
  };

  printf("test prefix_match:\n");

  const char *exp1[] = { NULL };
  failed += test_prefix_match(NULL, "", exp1, 0);
  failed += test_prefix_match(src, "x", exp1, 0);

  const char *exp2[] = { "a", NULL };
  failed += test_prefix_match(src, "a", exp2, 0);

  const char *exp3[] = { "b1", "b2", "b2a", NULL };
  failed += test_prefix_match(src, "b", exp3, 0);

  const char *exp4[] = { "b2", "b2a", NULL };
  failed += test_prefix_match(src, "b2", exp4, 0);

  const char *exp5[] = { "b2a", NULL };
  failed += test_prefix_match(src, "b2a", exp5, 0);
  failed += test_prefix_match(src, "b2b", exp1, 0);
  printf("\n");

  return failed;
}

// return 0 if test passes, 1 if test fails, -1 on internal error
int test_prefix_match(const char **src, const char *prefix, const char **exp, const int exp_exit)
{
  int exit;
  char **dst;

  if ((exit = prefix_match(&dst, src, prefix)) != exp_exit) {
    warnx("FAIL: %s = exit: %d, expected: %d\n", prefix, exit, exp_exit);
    if (exit != -1)
      free(dst);
    return 1;
  }

  if (arrcmp((const char**)dst, exp) == 0) {
    printf("PASS: %s\n", prefix);
    return 0;
  } else {
    warnx("FAIL: %s\n", prefix);
    return 1;
  }

  return -1;
}

// compare two null terminated string arrays, containing null terminated strings
// return 0 if both are equal, 1 if not, -1 on error.
int arrcmp(const char **a1, const char **a2)
{
  size_t i;
  int eql;

  eql = 0;

  for (i = 0; a1[i]; i++) {
    if (a2[i] == NULL) {
      eql = 1;
      break;
    }
    if (strcmp(a1[i], a2[i]) != 0) {
      eql = 1;
      break;
    }
  }
  if (a2[i] != NULL)
    eql = 1;

  return eql;
}
