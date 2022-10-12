#include "../parse_path.c"

#include <err.h>
#include <stdio.h>
#include <string.h>

#ifdef VERBOSE
static int verbose = 1;
#else
static int verbose = 0;
#endif

struct expfmt {
	const char *path;	/* path string */
	const path_t exppath;	/* expected path */
	int exitcode;
};

static struct expfmt exps[] = {
	{ "",                     { "", "" },                 -1 },
	{ "db",                   { "", "" },                 -1 },
	{ " /",                   { "", "" },                 -1 },
	{ "/db/coll",             { "db", "coll" },            0 },
	{ "/db/coll/",            { "db", "coll/" },           0 },
	{ "/",                    { "", "" },                  0 },
	{ "/ ",                   { " ", "" },                 0 },
	{ "/some ",               { "some ", "" },             0 },
	{ "/some/o ",             { "some", "o " },            0 },
	{ "/some",                { "some", "" },              0 },
	{ "/",                    { "", "" },                  0 },
	{ "/£",                   { "£", "" },                 0 },
	{ "/£/£",                 { "£", "£" },                0 }, /* two bytes in UTF-8 */
	{ "/＄/＄",               { "＄", "＄" },              0 }, /* three bytes in UTF-8 */
	{ "/£ह€한𐍈＄/£ह€한𐍈＄",   { "£ह€한𐍈＄", "£ह€한𐍈＄" },  0 }
};

struct expfmt2 {
	char *cpath;
	const char *npath;
	const char *exppath;
	int expcomps;
	size_t exitcode;
};

static struct expfmt2 exps2[] = {
	{ "", "/", "/", 0, 1 },
	{ "", "/a", "/a", 1, 2 },
	{ "", "/a/b", "/a/b", 2, 4 },
	{ "", "/foo/bar", "/foo/bar", 2, 8 },
	{ "", "/a/", "/a", 1, 2 },
	{ "", "///a///", "/a", 1, 2 },
	{ "", "///.///", "/", 0, 1 },
	{ "", "//", "/", 0, 1 },
	{ "", "/a/../b", "/b", 1, 2 },
	{ "", "/a/../../../b", "/b", 1, 2 },
	{ "", "/a/b/../../c/", "/c", 1, 2 },
	{ "", "///xyz/./../foo/bar/../../baz/", "/baz", 1, 4 },
	{ "", "/foo/../b", "/b", 1, 2 },
	{ "", "/foo/../b/bar/", "/b/bar", 2, 6 },

	{ "", "", "", 0, -1 },
	{ "/", "/", "/", 0, 1 },

	{ "/", "", "/", 0, 1 },
	{ "/", ".", "/", 0, 1 },
	{ "/", "a", "/a", 1, 2 },
	{ "/", "foo", "/foo", 1, 4 },
	{ "/foo", "bar", "/foo/bar", 2, 8 },
	{ "/foo/../b", "bar", "/b/bar", 2, 6 },
	{ "//foo//..///../..//..//b//", "bar", "/b/bar", 2, 6 },
};

struct expfmt3 {
	const path_t cpath;
	const char *av[2];
	const path_t expps[2];
	size_t exitcode;
};

static struct expfmt3 exps3[] = {
	{ {"a", ""},  {"/x/y", "."},           {{"x", "y"},{"a",""}},  0 },
	{ {"a", "b"}, {"/x/y", ""},            {{"x", "y"},{"a","b"}}, 0 },
	{ {"a", "b"}, {"/x/y", "."},           {{"x", "y"},{"a","b"}}, 0 },
	{ {"a", "b"}, {"/x/y", "../c"},        {{"x", "y"},{"a","c"}}, 0 },
	{ {"a", "b"}, {"/x/y", "../../c"},     {{"x", "y"},{"c",""}},  0 },
	{ {"a", "b"}, {"/x/y", "../../c/d"},   {{"x", "y"},{"c","d"}}, 0 },
	{ {"a", "b"}, {"../../c/d", "../y/"},  {{"c", "d"},{"a","y"}}, 0 },
	{ {"a", ""},  {"/x/y", ""},            {{"x", "y"},{"a",""}},  0 },
	{ {"a", ""},  {"../x", ""},            {{"x", ""}, {"a",""}},  0 }
};

/*
 * Return 0 if test passes, 1 if test fails.
 */
static int
test_parse_paths(path_t *ps[], const path_t cpath, const char **av,
    size_t avlen, const path_t *exp, const int exp_exit)
{
	size_t i;
	int exit;

	exit = parse_paths(ps, cpath, av, avlen);
	if (exit != exp_exit) {
		warnx("FAIL: \"/%s/%s\" = exit: %d, expected: %d\n",
		    cpath.dbname, cpath.collname, exit, exp_exit);
		return 1;
	}

	if (exit == exp_exit && exit == -1) {
		if (verbose)
			printf("PASS: \"/%s/%s\" = exit: %d, expected: %d\n",
			    cpath.dbname, cpath.collname, exit, exp_exit);

		return 0;
	}

	for (i = 0; i < avlen; i++) {
		if (strcmp((*ps)[i].dbname, exp[i].dbname) != 0 ||
		    strcmp((*ps)[i].collname, exp[i].collname) != 0) {
			warnx("FAIL: /%s/%s, expected /%s/%s\n",
			    (*ps)[i].dbname, (*ps)[i].collname,
			    exp[i].dbname, exp[i].collname);
			return 1;
		}
	}

	if (verbose)
		printf("PASS: \"/%s/%s\" %ld\n", cpath.dbname, cpath.collname,
		    avlen);

	return 0;
}

/*
 * return 0 if test passes, 1 if test fails, -1 on internal error
 */
static int
test_parse_path(const char *path, const path_t *exp, const int exp_exit)
{
	path_t npath;
	int exit;

	exit = parse_path(&npath, path);
	if (exit != exp_exit) {
		warnx("FAIL: \"%s\" = exit: %d, expected: %d\n", path, exit,
		    exp_exit);
		return 1;
	}

	if (exit == exp_exit && exit == -1) {
		if (verbose)
			printf("PASS: \"%s\" = exit: %d, expected: %d\n", path,
			    exit, exp_exit);

		return 0;
	}

	if (strcmp(npath.dbname, exp->dbname) == 0 && strcmp(npath.collname,
	    exp->collname) == 0) {
		if (verbose)
			printf("PASS: %s\n", path);

		return 0;
	} else {
		warnx("FAIL: \"%s\", db: \"%s\", coll: \"%s\"\n", path,
		    npath.dbname, npath.collname);
		return 1;
	}

	return -1;
}

/*
 * return 0 if test passes, 1 if test fails.
 */
static int
test_resolvepath(char *cpath, size_t cpathsize, const char *newpath,
    const char *exp, const int exp_comps, const size_t exp_exit)
{
	size_t exit;
	int comps;

	exit = resolvepath(cpath, cpathsize, newpath, &comps);

	if (exit == exp_exit && exit == (size_t)-1) {
		if (verbose)
			printf("PASS -1 \"%s\"\n", newpath);

		return 0;
	} else if (exit == exp_exit && exit >= cpathsize) {
		if (verbose)
			printf("PASS overflow \"%s\"\n", newpath);

		return 0;
	} else if (exit == exp_exit && strcmp(cpath, exp) == 0 &&
	    comps == exp_comps) {
		if (verbose)
			printf("PASS equal \"%s\"\n", cpath);

		return 0;
	}

	if (exit == (size_t)-1) {
		warnx("FAIL \"%s\": %ld, expected \"%s\" (%ld)\n", newpath,
		    exit, exp, exp_exit);
	} else if (exit > 0 && exit >= cpathsize) {
		warnx("FAIL \"%s\": overflow %ld, expected \"%s\" (%ld)\n",
		    newpath, exit, exp, exp_exit);
	} else {
		warnx("FAIL \"%s\": exit %ld, have \"%s\", comps %d, want %d, "
		    "expected \"%s\" %ld\n", newpath, exit, cpath, comps,
		    exp_comps, exp, exp_exit);
	}

	return 1;
}

int
main(void)
{
	char path[1000];
	struct expfmt *cexp;
	struct expfmt2 *cexp2;
	struct expfmt3 *cexp3;
	int total, failed, i;
	path_t *ps;

	failed = 0;

	cexp = exps;
	total = sizeof(exps) / sizeof(exps[0]);
	if (verbose)
		printf("test parse_path %d:\n", total);

	for (i = 0; i < total; i++) {
		failed += test_parse_path(cexp->path, &cexp->exppath,
		    cexp->exitcode);
		cexp++;
	}

	if (verbose)
		printf("\ntest resolvepath:\n");

	cexp2 = exps2;
	total = sizeof(exps2) / sizeof(exps2[0]);
	for (i = 0; i < total; i++) {
		memcpy(path, cexp2->cpath, strlen(cexp2->cpath) + 1);
		failed += test_resolvepath(path, sizeof(path), cexp2->npath,
		    cexp2->exppath, cexp2->expcomps, cexp2->exitcode);
		cexp2++;
	}

	if (verbose)
		printf("\ntest parse_paths:\n");

	cexp3 = exps3;
	total = sizeof(exps3) / sizeof(exps3[0]);
	for (i = 0; i < total; i++) {
		failed += test_parse_paths(&ps, cexp3->cpath, cexp3->av, 2,
		    cexp3->expps, cexp3->exitcode);
		free(ps);
		cexp3++;
	}

	return failed;
}
