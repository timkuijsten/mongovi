#include "../shorten.c"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXSTR 100

int test_shorten(const char *input, const int maxlen, const char *exp, const int exp_exit, const char *msg);
int test_shorten_comps(const char *comp1, const char *comp2, const int maxlen, const char *exp1, const char *exp2, const int exp_exit, const char *msg);

int
main()
{
	int failed = 0;

	setlocale(LC_CTYPE, "");

	printf("test shorten:\n");
	failed += test_shorten("", 1, "", -1, "");
	failed += test_shorten("foo", 1, "foo", -1, "");
	failed += test_shorten("", 5, "", 0, "");
	failed += test_shorten("foo", 4, "foo", 3, "");
	failed += test_shorten("foobar", 4, "f..r", 4, "");
	failed += test_shorten("foobar", 5, "fo..r", 5, "");
	failed += test_shorten("foobarqux", 4, "f..x", 4, "");
	failed += test_shorten("foobarqux", 7, "foo..ux", 7, "");
	failed += test_shorten("a longer sentence", 19, "a longer sentence", 17, "");
	failed += test_shorten("a longer sentence", 18, "a longer sentence", 17, "");
	failed += test_shorten("a longer sentence", 17, "a longer sentence", 17, "");
	failed += test_shorten("a longer sentence", 16, "a longe..entence", 16, "");
	failed += test_shorten("a longer sentence", 15, "a longe..ntence", 15, "");
	failed += test_shorten("a longer sentence", 14, "a long..ntence", 14, "");
	failed += test_shorten("a longer sentence", 13, "a long..tence", 13, "");
	failed += test_shorten("a longer sentence", 12, "a lon..tence", 12, "");
	failed += test_shorten("a longer sentence", 11, "a lon..ence", 11, "");
	failed += test_shorten("a longer sentence", 10, "a lo..ence", 10, "");
	failed += test_shorten("a longer sentence", 0, "a longer sentence", -1, "");
	failed += test_shorten("a longer sentence", 1, "a longer sentence", -1, "");
	failed += test_shorten("a longer sentence", 2, "..", 2, "");
	failed += test_shorten("a longer sentence", 3, "a..", 3, "");
	failed += test_shorten("a longer sentence", 4, "a..e", 4, "");

	/* UTF-8 tests */
	failed += test_shorten("한", 2, "한", 2, "");
	failed += test_shorten("한", 3, "한", 2, "");
	failed += test_shorten("한한", 2, "..", 2, "");
	failed += test_shorten("한한", 3, "..", 2, "");
	failed += test_shorten("한한", 4, "한한", 4, "");
	failed += test_shorten("한한한한", 4, "한..", 4, "");
	failed += test_shorten("한한한한", 5, "한..", 4, "");
	failed += test_shorten("한한한한", 6, "한..한", 6, "");
	failed += test_shorten("한한한한", 7, "한..한", 6, "");
	failed += test_shorten("한한한한", 8, "한한한한", 8, "");

	failed += test_shorten("£", 2, "£", 1, "");
	failed += test_shorten("£한", 2, "..", 2, "");
	failed += test_shorten("£한", 3, "£한", 3, "");
	failed += test_shorten("£한한", 2, "..", 2, "");
	failed += test_shorten("£한한", 3, "£..", 3, "");
	failed += test_shorten("£한한", 4, "£..", 3, "");
	failed += test_shorten("£한한", 5, "£한한", 5, "");

	failed += test_shorten("한£", 2, "..", 2, "");
	failed += test_shorten("한£", 3, "한£", 3, "");
	failed += test_shorten("한한£", 2, "..", 2, "");
	failed += test_shorten("한한£", 3, "..£", 3, "");
	failed += test_shorten("한한£", 4, "..£", 3, "");	/* choices */
	failed += test_shorten("한한£", 5, "한한£", 5, "");

	failed += test_shorten("£ह€한𐍈＄", 4, "£ह..", 4, "");
	failed += test_shorten("£ह€한𐍈＄", 5, "£..＄", 5, "");
	failed += test_shorten("£ह€한𐍈＄", 6, "£ह..＄", 6, "");
	failed += test_shorten("£ह€한𐍈＄", 7, "£ह€..＄", 7, "");
	failed += test_shorten("£ह€한𐍈＄", 8, "£ह€한𐍈＄", 8, "");
	printf("\n");

	printf("test shorten_comps:\n");
	failed += test_shorten_comps("f", "b", 8, "f", "b", 2, "");
	failed += test_shorten_comps("foof", "barb", 8, "foof", "barb", 8, "");
	failed += test_shorten_comps("foof", "barba", 8, "foof", "b..a", 8, "");
	failed += test_shorten_comps("foof", "barbaz", 8, "foof", "b..z", 8, "");
	failed += test_shorten_comps("foobar", "barbaz", 8, "f..r", "b..z", 8, "");
	failed += test_shorten_comps("foobar", "z", 8, "foobar", "z", 7, "");
	failed += test_shorten_comps("foobarfoobar", "z", 8, "foo..ar", "z", 8, "");
	failed += test_shorten_comps("fu", "barbaz", 8, "fu", "barbaz", 8, "");
	failed += test_shorten_comps("fu", "quxquuzraboof", 8, "fu", "qu..of", 8, "");

	failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 23, "foobarbaz", "quxquuzraboof", 22, "");
	failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 22, "foobarbaz", "quxquuzraboof", 22, "");
	failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 21, "foobarbaz", "quxqu..aboof", 21, "");
	failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 20, "foobarbaz", "quxqu..boof", 20, "");
	failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 19, "foo..az", "quxqu..aboof", 19, "");
	failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 18, "foo..az", "quxqu..boof", 18, "");
	failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 17, "foo..az", "quxq..boof", 17, "");
	failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 16, "fo..az", "quxq..boof", 16, "");
	failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 15, "fo..az", "quxq..oof", 15, "");

	failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 9, "f..z", "qu..f", 9, "");
	failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 8, "f..z", "q..f", 8, "");

	/* min 8 columns */
	failed += test_shorten_comps("foobarbaz", "quxquuzraboof", 7, "foobarbaz", "quxquuzraboof", -1, "");

	failed += test_shorten_comps("£ह€한𐍈＄£ह€한𐍈＄", "＄", 8, "£ह..＄", "＄", 8, "");
	failed += test_shorten_comps("£＄ह€", "한𐍈＄£ह€한𐍈＄", 8, "£..€", "한..", 8, "");
	failed += test_shorten_comps("＄", "£ह€한𐍈＄£ह€한𐍈＄", 8, "＄", "£ह..＄", 8, "");
	failed += test_shorten_comps("＄£ह€", "한𐍈＄£ह€한𐍈＄", 8, "..ह€", "한..", 8, "");
	failed += test_shorten_comps("＄£ह€", "한𐍈＄£ह€한𐍈＄", 9, "＄£ह€", "한..", 9, "");
	failed += test_shorten_comps("＄£ह€", "한𐍈＄£ह€한𐍈＄", 10, "＄£ह€", "한𐍈..", 10, "");
	failed += test_shorten_comps("＄£ह€", "한𐍈＄£ह€한𐍈＄", 11, "＄£ह€", "한..＄", 11, "");
	failed += test_shorten_comps("＄£ह€", "한𐍈＄£ह€한𐍈＄", 12, "＄£ह€", "한𐍈..＄", 12, "");
	failed += test_shorten_comps("＄£ह€", "한𐍈＄£ह€한𐍈＄", 13, "＄£ह€", "한𐍈..𐍈＄", 13, "");
	failed += test_shorten_comps("＄£ह€", "한𐍈＄£ह€한𐍈＄", 14, "＄£ह€", "한𐍈..𐍈＄", 13, "");
	failed += test_shorten_comps("＄£ह€", "한𐍈＄£ह€한𐍈＄", 15, "＄£ह€", "한𐍈＄..𐍈＄", 15, "");

	return failed;
}

/*
 * return 0 if test passes, 1 if test fails, -1 on internal error
 */
int
test_shorten(const char *input, const int maxlen, const char *exp, const int exp_exit, const char *msg)
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

/*
 * return 0 if test passes, 1 if test fails, -1 on internal error
 */
int
test_shorten_comps(const char *comp1, const char *comp2, const int maxlen, const char *exp1, const char *exp2, const int exp_exit, const char *msg)
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
