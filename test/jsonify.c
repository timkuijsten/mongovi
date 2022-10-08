#include "../jsonify.c"

#define MAXSTR 1024

#ifdef VERBOSE
static int verbose = 1;
#else
static int verbose = 0;
#endif

/*
 * return 0 if test passes, 1 if test fails, -1 on internal error
 */
static int
test_relaxed_to_strict(const char *input, size_t inputlen, int maxobj,
    const char *exp, const int exp_exit, const char *msg)
{
	char dst[MAXSTR];
	int exit;

	if (inputlen >= MAXSTR)
		abort();

	exit = relaxed_to_strict(dst, sizeof(dst), input, inputlen, maxobj);
	if (exit != exp_exit) {
		fprintf(stderr, "FAIL: %s %d = exit: %d, expected: %d\t%s\n",
		    input, maxobj, exit, exp_exit, msg);
		return 1;
	}

	if (strcmp(dst, exp) == 0) {
		if (verbose)
			printf("PASS: %s %d = \"%s\"\t%s\n", input, maxobj, dst,
			    msg);

		return 0;
	} else {
		fprintf(stderr, "FAIL: %s %d = \"%s\" instead of \"%s\"\t%s\n",
		    input, maxobj, dst, exp, msg);
		return 1;
	}

	return -1;
}

int
main(void)
{
	char *doc, *exp;
	int failed = 0;

	if (verbose)
		printf("test relaxed_to_strict:\n");

	doc = "{ a: 'b' }";
	exp = "{\"a\":\"b\"}";
	failed += test_relaxed_to_strict(doc, strlen(doc), -1, exp, 11, "");

	doc = "{ a: b }";
	exp = "{\"a\":b}";
	failed += test_relaxed_to_strict(doc, strlen(doc), -1, exp, 9, "");

	doc = "{ a: b }{ c: d }";
	exp = "{\"a\":b}{\"c\":d}";
	failed += test_relaxed_to_strict(doc, strlen(doc), -1, exp, 17, "");

	doc = "{ a: { c: d } }";
	exp = "{\"a\":{\"c\":d}}";
	failed += test_relaxed_to_strict(doc, strlen(doc), -1, exp, 16, "");

	doc = "{ a: { c: d } }  { a: { c: d } }";
	exp = "{\"a\":{\"c\":d}}{\"a\":{\"c\":d}}";
	failed += test_relaxed_to_strict(doc, strlen(doc), -1, exp, 33, "");

	doc = "{ a: { c: d } }  { a: { c: d } }";
	exp = "{\"a\":{\"c\":d}}";
	failed += test_relaxed_to_strict(doc, strlen(doc), 1, exp, 16, "");

	doc = "{ a: { c: { e: f } } }{ a: { c: { e: f } }}  { a: { c: { e: f } "
	    "}}";

	exp = "{\"a\":{\"c\":{\"e\":f}}}{\"a\":{\"c\":{\"e\":f}}}{\"a\":{\"c\":"
	    "{\"e\":f}}}";
	failed += test_relaxed_to_strict(doc, strlen(doc), -1, exp, 67, "");
	failed += test_relaxed_to_strict(doc, strlen(doc), 3, exp, 67, "");
	failed += test_relaxed_to_strict(doc, strlen(doc), 4, exp, 67, "");

	exp = "{\"a\":{\"c\":{\"e\":f}}}";
	failed += test_relaxed_to_strict(doc, strlen(doc), 1, exp, 23, "");

	exp = "{\"a\":{\"c\":{\"e\":f}}}{\"a\":{\"c\":{\"e\":f}}}";
	failed += test_relaxed_to_strict(doc, strlen(doc), 2, exp, 44, "");

	/* UTF-8 */
	/*
	doc = "{ 한: '＄' }";
	exp = "{ \"한\": \"＄\" }";
	failed += test_relaxed_to_strict(doc, strlen(doc), -1, exp, 30, "");
	*/

	return failed;
}
