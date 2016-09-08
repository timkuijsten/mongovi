#include "../shorten.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXSTR 100

int test_shorten(const char *input, const int maxlen, const char *exp, const int exp_exit, const char *msg);
int test_shorten_comps(const char *comp1, const char *comp2, const int maxlen, const char *exp1, const char *exp2, const int exp_exit, const char *msg);

int main()
{
  int failed = 0;

  printf("test shorten:\n");
  failed += test_shorten("", 3, "", -1, "");
  failed += test_shorten("foo", 3, "foo", -1, "");
  failed += test_shorten("", 5, "", 0, "");
  failed += test_shorten("foo", 4, "foo", 3, "");
  failed += test_shorten("foobar", 4, "f..r", 4, "");
  failed += test_shorten("foobar", 5, "f..ar", 5, "");
  failed += test_shorten("foobarqux", 4, "f..x", 4, "");
  failed += test_shorten("foobarqux", 7, "fo..qux", 7, "");
  failed += test_shorten("a longer sentence", 19, "a longer sentence", 17, "");
  failed += test_shorten("a longer sentence", 18, "a longer sentence", 17, "");
  failed += test_shorten("a longer sentence", 17, "a longer sentence", 17, "");
  failed += test_shorten("a longer sentence", 16, "a longe..entence", 16, "");
  failed += test_shorten("a longer sentence", 15, "a long..entence", 15, "");
  failed += test_shorten("a longer sentence", 14, "a long..ntence", 14, "");
  failed += test_shorten("a longer sentence", 13, "a lon..ntence", 13, "");
  failed += test_shorten("a longer sentence", 12, "a lon..tence", 12, "");
  failed += test_shorten("a longer sentence", 11, "a lo..tence", 11, "");
  failed += test_shorten("a longer sentence", 10, "a lo..ence", 10, "");
  failed += test_shorten("a longer sentence", 0, "a longer sentence", -1, "");
  failed += test_shorten("a longer sentence", 1, "a longer sentence", -1, "");
  failed += test_shorten("a longer sentence", 2, "a longer sentence", -1, "");
  failed += test_shorten("a longer sentence", 3, "a longer sentence", -1, "");
  failed += test_shorten("a longer sentence", 4, "a..e", 4, "");
  printf("\n");

  printf("test shorten_comps:\n");
  failed += test_shorten_comps("foo", "bar", 6, "foo", "bar", -1, "");
  failed += test_shorten_comps("foobar", "barbaz", 8, "f..r", "b..z", 2, "");
  failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 23, "foobarbaz", "quxquuzraboof", 0, "");
  failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 22, "foobarbaz", "quxquuzraboof", 0, "");
  failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 21, "foo..baz", "quxquuzraboof", 1, "");
  failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 20, "fo..baz", "quxquuzraboof", 1, "");

  failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 18, "f..az", "quxquuzraboof", 1, "");
  failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 17, "f..z", "quxquuzraboof", 1, "");
  failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 16, "f..z", "quxqu..aboof", 2, "");
  failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 15, "f..z", "quxq..aboof", 2, "");

  failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 9, "f..z", "q..of", 2, "");
  failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 8, "f..z", "q..f", 2, "");
  failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 7, "foobarbaz", "quxquuzraboof", -1, "");

  failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 0, "foobarbaz", "quxquuzraboof", -1, "");

  return failed;
}

// return 0 if test passes, 1 if test fails, -1 on internal error
int test_shorten(const char *input, const int maxlen, const char *exp, const int exp_exit, const char *msg)
{
  int exit;
  char str[MAXSTR];

  strcpy(str, input);
  if ((exit = shorten(str, maxlen)) != exp_exit) {
    fprintf(stderr, "FAIL: %s %d = exit: %d, expected: %d\t%s\n", input, maxlen, exit, exp_exit, msg);
    return 1;
  }

  if (strcmp(str, exp) == 0) {
    printf("PASS: %s %d = \"%s\"\t%s\n", input, maxlen, str, msg);
    return 0;
  } else {
    fprintf(stderr, "FAIL: %s %d = \"%s\" instead of \"%s\"\t%s\n", input, maxlen, str, exp, msg);
    return 1;
  }

  return -1;
}

// return 0 if test passes, 1 if test fails, -1 on internal error
int test_shorten_comps(const char *comp1, const char *comp2, const int maxlen, const char *exp1, const char *exp2, const int exp_exit, const char *msg)
{
  int exit;
  char c1[MAXSTR];
  char c2[MAXSTR];
  strcpy(c1, comp1);
  strcpy(c2, comp2);

  if ((exit = shorten_comps(c1, c2, maxlen)) != exp_exit) {
    fprintf(stderr, "FAIL: %s %s %d = exit: %d, expected: %d\t%s\n", comp1, comp2, maxlen, exit, exp_exit, msg);
    return 1;
  }

  if (strcmp(c1, exp1) == 0 && strcmp(c2, exp2) == 0) {
    printf("PASS: %s %s %d = \"%s\" \"%s\" \t%s\n", comp1, comp2, maxlen, c1, c2, msg);
    return 0;
  } else {
    fprintf(stderr, "FAIL: %s %s %d = \"%s\" and \"%s\" instead of \"%s\" and \"%s\" \t%s\n", comp1, comp2, maxlen, c1, c2, exp1, exp2, msg);
    return 1;
  }

  return -1;
}
