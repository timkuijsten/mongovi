#include "../mongovi.h"

#include <err.h>
#include <stdio.h>
#include <string.h>

int test_parse_path(const char *path, path_t *newpath, const path_t *exp, const int exp_exit);

int main()
{
  /* list of expectations */
  const path_t exps[] = {
    { "db", "coll" },
    { "db", "coll/" },
    { "db", "coll" },
    { "foo", "bar/coll" },
    { "foo", "bar/coll" }
  };
  /* current expectation */
  const path_t *cexp;

  int failed = 0;
  cexp = exps;
  printf("test parse_path:\n");

  path_t np1 = { "", "" };
  failed += test_parse_path("/db/coll", &np1, cexp++, 0);

  path_t np2 = { "", "" };
  failed += test_parse_path("/db/coll/", &np2, cexp++, 0);

  path_t np3 = { "", "" };
  failed += test_parse_path("db/coll", &np3, cexp++, 0);

  path_t np4 = { "foo", "" };
  failed += test_parse_path("bar/coll", &np4, cexp++, 0);

  path_t np5 = { "foo", "some" };
  failed += test_parse_path("bar/coll", &np5, cexp++, 0);

  return failed;
}

// return 0 if test passes, 1 if test fails, -1 on internal error
int
test_parse_path(const char *path, path_t *newpath, const path_t *exp, const int exp_exit)
{
  int exit;

  if ((exit = parse_path(path, newpath)) != exp_exit) {
    warnx("FAIL: %s = exit: %d, expected: %d\n", path, exit, exp_exit);
    return 1;
  }

  if (strcmp(newpath->dbname, exp->dbname) == 0 && strcmp(newpath->collname, exp->collname) == 0) {
    printf("PASS: %s\n", path);
    return 0;
  } else {
    warnx("FAIL: %s, db: %s, coll: %s\n", path, newpath->dbname, newpath->collname);
    return 1;
  }

  return -1;
}
