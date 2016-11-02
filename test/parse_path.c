#include "../mongovi.h"

#include <err.h>
#include <stdio.h>
#include <string.h>

int test_parse_path(const char *path, path_t *newpath, const path_t *exp, const int exp_exit);

struct expfmt {
  const char *paths;    /* path string */
  path_t npath;         /* new path */
  const path_t exppath; /* expected path */
};

int main()
{
  int total, failed, i;

  /* list of expectations */
  struct expfmt exps[] = {
    { "/db/coll",                        { "", "" },         { "db", "coll" } },
    { "/db/coll/",                       { "", "" },         { "db", "coll/" } },
    { "db/coll",                         { "", "" },         { "db", "coll" } },
    { "bar/coll",                        { "foo", "" },      { "foo", "bar/coll" } },
    { "bar/coll",                        { "foo", "some" },  { "foo", "bar/coll" } },
    { "bar/../baz/coll",                 { "", "" },         { "baz", "coll" } },
    { "bar/../baz/coll",                 { "", "" },         { "baz", "coll" } },
    { "bar/../baz/../foo/raboof",        { "", "" },         { "foo", "raboof" } },
    { "../../bar/../baz/../foo/raboof",  { "", "" },         { "foo", "raboof" } },
    { "/some/coll/../some",              { "", "" },         { "some", "coll/../some" } },
    { "../other",                        { "foo", "bar" },   { "foo", "other" } },
    { "../other",                        { "foo", "" },      { "other", "" } },
    { "../other/coll",                   { "foo", "" },      { "other", "coll" } },
    { "/some/coll/../some",              { "foo", "bar" },   { "some", "coll/../some" } },
    { "some/coll/../some",               { "foo", "bar" },   { "foo", "some/coll/../some" } },
    { "../som/../some",                  { "foo", "bar" },   { "foo", "som/../some" } }
  };
  /* current expectation */
  struct expfmt *cexp;

  total = sizeof(exps) / sizeof(exps[0]);
  cexp = exps;
  printf("test parse_path %d:\n", total);

  failed = 0;
  for (i = 0; i < total; i++) {
    failed += test_parse_path(cexp->paths, &cexp->npath, &cexp->exppath, 0);
    cexp++;
  }

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
