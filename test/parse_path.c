#include "../mongovi.h"

#include <err.h>
#include <stdio.h>
#include <string.h>

int test_parse_path(const char *path, path_t *newpath, const path_t *exp, const int exp_exit);

int main()
{
  int failed = 0;
  printf("test parse_path:\n");

  const path_t exp1 = { "db", "coll" };
  path_t np1 = { "", "" };
  failed += test_parse_path("/db/coll", &np1, &exp1, 0);

  const path_t exp2 = { "db", "coll/" };
  path_t np2 = { "", "" };
  failed += test_parse_path("/db/coll/", &np2, &exp2, 0);

  const path_t exp3 = { "db", "coll" };
  path_t np3 = { "", "" };
  failed += test_parse_path("db/coll", &np3, &exp3, 0);

  const path_t exp4 = { "foo", "bar/coll" };
  path_t np4 = { "foo", "" };
  failed += test_parse_path("bar/coll", &np4, &exp4, 0);

  const path_t exp5 = { "foo", "bar/coll" };
  path_t np5 = { "foo", "some" };
  failed += test_parse_path("bar/coll", &np5, &exp5, 0);

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
