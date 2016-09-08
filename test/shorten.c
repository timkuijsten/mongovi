#include "../shorten.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXSTR 100

int test_shorten(const char *input, const int maxlen, const char *exp, const int exp_exit, const char *msg);

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
