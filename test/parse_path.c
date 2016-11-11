#include "../mongovi.h"

#include <err.h>
#include <stdio.h>
#include <string.h>

int test_parse_path(const char *path, path_t *newpath, int dsi, int csi, const path_t *exp, const int exp_exit);

struct expfmt {
  const char *paths;    /* path string */
  path_t npath;         /* new path */
  const path_t exppath; /* expected path */
  int dsi;              /* db start index */
  int csi;              /* coll start index */
};

int main()
{
  int total, failed, i;

  /* list of expectations */
  struct expfmt exps[] = {
    { "/db/coll",                        { "", "" },         { "db", "coll" },                 1,  4 },
    { "/db/coll/",                       { "", "" },         { "db", "coll/" },                1,  4 },
    { "db/coll",                         { "", "" },         { "db", "coll" },                 0,  3 },
    { "bar/coll",                        { "foo", "" },      { "foo", "bar/coll" },           -1,  0 },
    { "bar/coll",                        { "foo", "some" },  { "foo", "bar/coll" },           -1,  0 },
    { "/",                               { "", "" },         { "", "" },                      -1, -1 },
    { "/",                               { "foo", "" },      { "", "" },                      -1, -1 },
    { "/",                               { "foo", "some" },  { "", "" },                      -1, -1 },
    { "bar/../baz/coll",                 { "", "" },         { "baz", "coll" },                7, 11 },
    { "bar/../baz/coll",                 { "", "" },         { "baz", "coll" },                7, 11 },
    { "bar/../baz/../foo/raboof",        { "", "" },         { "foo", "raboof" },             14, 18 },
    { "../../bar/../baz/../foo/raboof",  { "", "" },         { "foo", "raboof" },             20, 24 },
    { "/some/coll/../some",              { "", "" },         { "some", "coll/../some" },       1,  6 },
    { "../other",                        { "foo", "bar" },   { "foo", "other" },              -1,  3 },
    { "..",                              { "foo", "" },      { "", "" },                      -1, -1 },
    { "../",                             { "foo", "" },      { "", "" },                      -1, -1 },
    { "..",                              { "foo", "bar" },   { "foo", "" },                   -1, -1 },
    { "../other",                        { "foo", "" },      { "other", "" },                  3, -1 },
    { "../other/coll",                   { "foo", "" },      { "other", "coll" },              3,  9 },
    { "/some/coll/../some",              { "foo", "bar" },   { "some", "coll/../some" },       1,  6 },
    { "some/coll/../some",               { "foo", "bar" },   { "foo", "some/coll/../some" },  -1,  0 },
    { "../som/../some",                  { "foo", "bar" },   { "foo", "som/../some" },        -1,  3 },
    { "../../../baz/../some",            { "foo", "bar" },   { "some", "" },                  16, -1 }
  };
  /* current expectation */
  struct expfmt *cexp;

  total = sizeof(exps) / sizeof(exps[0]);
  cexp = exps;
  printf("test parse_path %d:\n", total);

  failed = 0;
  for (i = 0; i < total; i++) {
    failed += test_parse_path(cexp->paths, &cexp->npath, cexp->dsi, cexp->csi, &cexp->exppath, 0);
    cexp++;
  }

  return failed;
}

// return 0 if test passes, 1 if test fails, -1 on internal error
int
test_parse_path(const char *path, path_t *newpath, int dsi, int csi, const path_t *exp, const int exp_exit)
{
  int exit;

  int i, j;
  if ((exit = parse_path(path, newpath, &i, &j)) != exp_exit) {
    warnx("FAIL: %s = exit: %d, expected: %d\n", path, exit, exp_exit);
    return 1;
  }

  if (strcmp(newpath->dbname, exp->dbname) == 0 && strcmp(newpath->collname, exp->collname) == 0 && i == dsi && j == csi) {
    printf("PASS: %s\n", path);
    return 0;
  } else {
    warnx("FAIL: %s, db: %s (%d), coll: %s (%d)\n", path, newpath->dbname, i, newpath->collname, j);
    return 1;
  }

  return -1;
}
