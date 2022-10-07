#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "structs.h"
#include "globals.c"

static void test_af_null_features_ptr(void **state)
{
	assert_int_equal(add_feature(NULL, "test"), 1);
}

static void af_helper(const char *features_start, const char *addition,
		      const char *features_end, int result)
{
	char *f = NULL, *orig = NULL;

	if (features_start) {
		f = strdup(features_start);
		assert_non_null(f);
		orig = f;
	}
	assert_int_equal(add_feature(&f, addition), result);
	if (result != 0 || features_end == NULL)
		assert_ptr_equal(orig, f);
	else
		assert_string_equal(f, features_end);
	free(f);
}

static void test_af_null_addition1(void **state)
{
	af_helper("0", NULL, NULL, 0);
}

static void test_af_null_addition2(void **state)
{
	af_helper("1 queue_if_no_path", NULL, NULL, 0);
}

static void test_af_empty_addition(void **state)
{
	af_helper("2 pg_init_retries 5", "", NULL, 0);
}

static void test_af_invalid_addition1(void **state)
{
	af_helper("2 pg_init_retries 5", " ", NULL, 1);
}

static void test_af_invalid_addition2(void **state)
{
	af_helper("2 pg_init_retries 5", "\tbad", NULL, 1);
}

static void test_af_invalid_addition3(void **state)
{
	af_helper("2 pg_init_retries 5", "bad ", NULL, 1);
}
 
static void test_af_invalid_addition4(void **state)
{
	af_helper("2 pg_init_retries 5", " bad ", NULL, 1);
}
 
static void test_af_null_features1(void **state)
{
	af_helper(NULL, "test", "1 test", 0);
}

static void test_af_null_features2(void **state)
{
	af_helper(NULL, "test\t  more", "2 test\t  more", 0);
}

static void test_af_null_features3(void **state)
{
	af_helper(NULL, "test\neven\tmore", "3 test\neven\tmore", 0);
}

static void test_af_already_exists1(void **state)
{
	af_helper("4 this is a test", "test", NULL, 0);
}

static void test_af_already_exists2(void **state)
{
	af_helper("5 contest testy intestine test retest", "test", NULL, 0);
}

static void test_af_almost_exists(void **state)
{
	af_helper("3 contest testy intestine", "test",
		  "4 contest testy intestine test", 0);
}

static void test_af_bad_features1(void **state)
{
	af_helper("bad", "test", NULL, 1);
}

static void test_af_bad_features2(void **state)
{
	af_helper("1bad", "test", NULL, 1);
}

static void test_af_add1(void **state)
{
	af_helper("0", "test", "1 test", 0);
}

static void test_af_add2(void **state)
{
	af_helper("0", "this is a test", "4 this is a test", 0);
}

static void test_af_add3(void **state)
{
	af_helper("1 features", "more values", "3 features more values", 0);
}

static void test_af_add4(void **state)
{
	af_helper("2 one\ttwo", "three\t four", "4 one\ttwo three\t four", 0);
}

static int test_add_features(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_af_null_features_ptr),
		cmocka_unit_test(test_af_null_addition1),
		cmocka_unit_test(test_af_null_addition2),
		cmocka_unit_test(test_af_empty_addition),
		cmocka_unit_test(test_af_invalid_addition1),
		cmocka_unit_test(test_af_invalid_addition2),
		cmocka_unit_test(test_af_invalid_addition3),
		cmocka_unit_test(test_af_invalid_addition4),
		cmocka_unit_test(test_af_null_features1),
		cmocka_unit_test(test_af_null_features2),
		cmocka_unit_test(test_af_null_features3),
		cmocka_unit_test(test_af_already_exists1),
		cmocka_unit_test(test_af_already_exists2),
		cmocka_unit_test(test_af_almost_exists),
		cmocka_unit_test(test_af_bad_features1),
		cmocka_unit_test(test_af_bad_features2),
		cmocka_unit_test(test_af_add1),
		cmocka_unit_test(test_af_add2),
		cmocka_unit_test(test_af_add3),
		cmocka_unit_test(test_af_add4),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}

static void test_rf_null_features_ptr(void **state)
{
	assert_int_equal(remove_feature(NULL, "test"), 1);
}

static void test_rf_null_features(void **state)
{
	char *f = NULL;
	assert_int_equal(remove_feature(&f, "test"), 1);
}

static void rf_helper(const char *features_start, const char *removal,
		      const char *features_end, int result)
{
	char *f = strdup(features_start);
	char *orig = f;

	assert_non_null(f);
	assert_int_equal(remove_feature(&f, removal), result);
	if (result != 0 || features_end == NULL)
		assert_ptr_equal(orig, f);
	else
		assert_string_equal(f, features_end);
	free(f);
}

static void test_rf_null_removal(void **state)
{
	rf_helper("1 feature", NULL, NULL, 0);
}

static void test_rf_empty_removal(void **state)
{
	rf_helper("1 feature", "", NULL, 0);
}

static void test_rf_invalid_removal1(void **state)
{
	rf_helper("1 feature", " ", NULL, 1);
}

static void test_rf_invalid_removal2(void **state)
{
	rf_helper("1 feature", " bad", NULL, 1);
}

static void test_rf_invalid_removal3(void **state)
{
	rf_helper("1 feature", "bad\n", NULL, 1);
}

static void test_rf_invalid_removal4(void **state)
{
	rf_helper("1 feature", "\tbad  \n", NULL, 1);
}

static void test_rf_bad_features1(void **state)
{
	rf_helper("invalid feature test string", "test", NULL, 1);
}

static void test_rf_bad_features2(void **state)
{
	rf_helper("2no space test", "test", NULL, 1);
}

static void test_rf_missing_removal1(void **state)
{
	rf_helper("0", "test", NULL, 0);
}

static void test_rf_missing_removal2(void **state)
{
	rf_helper("1 detest", "test", NULL, 0);
}

static void test_rf_missing_removal3(void **state)
{
	rf_helper("4 testing one two three", "test", NULL, 0);
}

static void test_rf_missing_removal4(void **state)
{
	rf_helper("1 contestant", "test", NULL, 0);
}

static void test_rf_missing_removal5(void **state)
{
	rf_helper("3 testament protest detestable", "test", NULL, 0);
}

static void test_rf_remove_all_features1(void **state)
{
	rf_helper("1 test", "test", "0", 0);
}

static void test_rf_remove_all_features2(void **state)
{
	rf_helper("2 another\t test", "another\t test", "0", 0);
}

static void test_rf_remove1(void **state)
{
	rf_helper("2 feature1 feature2", "feature2", "1 feature1", 0);
}

static void test_rf_remove2(void **state)
{
	rf_helper("2 feature1 feature2", "feature1", "1 feature2", 0);
}

static void test_rf_remove3(void **state)
{
	rf_helper("3 test1 test\ttest2", "test", "2 test1 test2", 0);
}

static void test_rf_remove4(void **state)
{
	rf_helper("4 this\t is a test", "is a", "2 this\t test", 0);
}

static void test_rf_remove5(void **state)
{
	rf_helper("3 one more test", "more test", "1 one", 0);
}

static int test_remove_features(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_rf_null_features_ptr),
		cmocka_unit_test(test_rf_null_features),
		cmocka_unit_test(test_rf_null_removal),
		cmocka_unit_test(test_rf_empty_removal),
		cmocka_unit_test(test_rf_invalid_removal1),
		cmocka_unit_test(test_rf_invalid_removal2),
		cmocka_unit_test(test_rf_invalid_removal3),
		cmocka_unit_test(test_rf_invalid_removal4),
		cmocka_unit_test(test_rf_bad_features1),
		cmocka_unit_test(test_rf_bad_features2),
		cmocka_unit_test(test_rf_missing_removal1),
		cmocka_unit_test(test_rf_missing_removal2),
		cmocka_unit_test(test_rf_missing_removal3),
		cmocka_unit_test(test_rf_missing_removal4),
		cmocka_unit_test(test_rf_missing_removal5),
		cmocka_unit_test(test_rf_remove_all_features1),
		cmocka_unit_test(test_rf_remove_all_features2),
		cmocka_unit_test(test_rf_remove1),
		cmocka_unit_test(test_rf_remove2),
		cmocka_unit_test(test_rf_remove3),
		cmocka_unit_test(test_rf_remove4),
		cmocka_unit_test(test_rf_remove5),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}

int main(void)
{
	int ret = 0;

	init_test_verbosity(-1);
	ret += test_add_features();
	ret += test_remove_features();

	return ret;
}
