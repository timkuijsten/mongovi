#include "../shorten.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXSTR 100

int run_test(const char *input, int maxlen, char *exp, int exp_exit, char *msg);

int main()
{
  int failed = 0;

  failed += run_test("", 3, "", -1, "");
  failed += run_test("foo", 3, "foo", -1, "");
  failed += run_test("", 5, "", 0, "");
  failed += run_test("foo", 4, "foo", 3, "");
  failed += run_test("foobar", 4, "f..r", 4, "");
  failed += run_test("foobar", 5, "f..ar", 5, "");
  failed += run_test("foobarqux", 4, "f..x", 4, "");
  failed += run_test("foobarqux", 7, "fo..qux", 7, "");
  failed += run_test("a longer sentence", 19, "a longer sentence", 17, "");
  failed += run_test("a longer sentence", 18, "a longer sentence", 17, "");
  failed += run_test("a longer sentence", 17, "a longer sentence", 17, "");
  failed += run_test("a longer sentence", 16, "a longe..entence", 16, "");
  failed += run_test("a longer sentence", 15, "a long..entence", 15, "");
  failed += run_test("a longer sentence", 14, "a long..ntence", 14, "");
  failed += run_test("a longer sentence", 13, "a lon..ntence", 13, "");
  failed += run_test("a longer sentence", 12, "a lon..tence", 12, "");
  failed += run_test("a longer sentence", 11, "a lo..tence", 11, "");
  failed += run_test("a longer sentence", 10, "a lo..ence", 10, "");
  failed += run_test("a longer sentence", 0, "a longer sentence", -1, "");
  failed += run_test("a longer sentence", 1, "a longer sentence", -1, "");
  failed += run_test("a longer sentence", 2, "a longer sentence", -1, "");
  failed += run_test("a longer sentence", 3, "a longer sentence", -1, "");
  failed += run_test("a longer sentence", 4, "a..e", 4, "");

  return failed;
}

// return 0 if test passes, 1 if test fails, -1 on internal error
int run_test(const char *input, int maxlen, char *exp, int exp_exit, char *msg)
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
