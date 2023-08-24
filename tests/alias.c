#include <pthread.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <cmocka.h>
#include "util.h"
#include "alias.h"
#include "test-log.h"
#include <errno.h>

#include "globals.c"
#include "../libmultipath/alias.c"

#if INT_MAX == 0x7fffffff
/* user_friendly_name for map #INT_MAX */
#define MPATH_ID_INT_MAX "fxshrxw"
/* ... and one less */
#define MPATH_ID_INT_MAX_m1 "fxshrxv"
/* ... and one more */
#define MPATH_ID_INT_MAX_p1 "fxshrxx"
#endif

void __wrap_rewind(FILE *stream)
{}

char *__wrap_fgets(char *buf, int n, FILE *stream)
{
	char *val = mock_ptr_type(char *);
	if (!val)
		return NULL;
	strlcpy(buf, val, n);
	return buf;
}

static int __set_errno(int err)
{
	if (err >= 0) {
		errno = 0;
		return err;
	} else {
		errno = -err;
		return -1;
	}
}

off_t __wrap_lseek(int fd, off_t offset, int whence)
{
	return __set_errno(mock_type(int));

}

ssize_t __wrap_write(int fd, const void *buf, size_t count)
{
	check_expected(count);
	check_expected(buf);
	return __set_errno(mock_type(int));
}

int __wrap_ftruncate(int fd, off_t length)
{
	check_expected(length);
	return __set_errno(mock_type(int));
}

int __wrap_dm_map_present(const char * str)
{
	check_expected(str);
	return mock_type(int);
}

int __wrap_dm_get_uuid(const char *name, char *uuid, int uuid_len)
{
	int ret;

	check_expected(name);
	check_expected(uuid_len);
	assert_non_null(uuid);
	ret = mock_type(int);
	if (ret == 0)
		strcpy(uuid, mock_ptr_type(char *));
	return ret;
}

#define TEST_FDNO 1234
#define TEST_FPTR ((FILE *) 0xaffe)

int __wrap_open_file(const char *file, int *can_write, const char *header)
{
	int cw = mock_type(int);

        *can_write = cw;
	return TEST_FDNO;
}

FILE *__wrap_fdopen(int fd, const char *mode)
{
	assert_int_equal(fd, TEST_FDNO);
	return TEST_FPTR;
}

int __wrap_fflush(FILE *f)
{
	assert_ptr_equal(f, TEST_FPTR);
	return 0;
}

int __wrap_fclose(FILE *f)
{
	assert_ptr_equal(f, TEST_FPTR);
	return 0;
}

/* strbuf wrapper for the old format_devname() */
static int __format_devname(char *name, int id, size_t len, const char *prefix)
{
	STRBUF_ON_STACK(buf);

	if (append_strbuf_str(&buf, prefix) < 0 ||
	    format_devname(&buf, id) < 0 ||
	    len <= get_strbuf_len(&buf))
		return -1;
	strcpy(name, get_strbuf_str(&buf));
	return get_strbuf_len(&buf);
}

static void fd_mpatha(void **state)
{
	char buf[32];
	int rc;

	rc = __format_devname(buf, 1, sizeof(buf), "FOO");
	assert_int_equal(rc, 4);
	assert_string_equal(buf, "FOOa");
}

static void fd_mpathz(void **state)
{
	/* This also tests a "short" buffer, see fd_mpath_short1 */
	char buf[5];
	int rc;

	rc = __format_devname(buf, 26, sizeof(buf), "FOO");
	assert_int_equal(rc, 4);
	assert_string_equal(buf, "FOOz");
}

static void fd_mpathaa(void **state)
{
	char buf[32];
	int rc;

	rc = __format_devname(buf, 26 + 1, sizeof(buf), "FOO");
	assert_int_equal(rc, 5);
	assert_string_equal(buf, "FOOaa");
}

static void fd_mpathzz(void **state)
{
	char buf[32];
	int rc;

	rc = __format_devname(buf, 26*26 + 26, sizeof(buf), "FOO");
	assert_int_equal(rc, 5);
	assert_string_equal(buf, "FOOzz");
}

static void fd_mpathaaa(void **state)
{
	char buf[32];
	int rc;

	rc = __format_devname(buf, 26*26 + 27, sizeof(buf), "FOO");
	assert_int_equal(rc, 6);
	assert_string_equal(buf, "FOOaaa");
}

static void fd_mpathzzz(void **state)
{
	char buf[32];
	int rc;

	rc = __format_devname(buf, 26*26*26 + 26*26 + 26, sizeof(buf), "FOO");
	assert_int_equal(rc, 6);
	assert_string_equal(buf, "FOOzzz");
}

static void fd_mpathaaaa(void **state)
{
	char buf[32];
	int rc;

	rc = __format_devname(buf, 26*26*26 + 26*26 + 27, sizeof(buf), "FOO");
	assert_int_equal(rc, 7);
	assert_string_equal(buf, "FOOaaaa");
}

static void fd_mpathzzzz(void **state)
{
	char buf[32];
	int rc;

	rc = __format_devname(buf, 26*26*26*26 + 26*26*26 + 26*26 + 26,
			    sizeof(buf), "FOO");
	assert_int_equal(rc, 7);
	assert_string_equal(buf, "FOOzzzz");
}

#ifdef MPATH_ID_INT_MAX
static void fd_mpath_max(void **state)
{
	char buf[32];
	int rc;

	rc  = __format_devname(buf, INT_MAX, sizeof(buf), "");
	assert_int_equal(rc, strlen(MPATH_ID_INT_MAX));
	assert_string_equal(buf, MPATH_ID_INT_MAX);
}
#endif

static void fd_mpath_max1(void **state)
{
	char buf[32];
	int rc;

	rc  = __format_devname(buf, INT_MIN, sizeof(buf), "");
	assert_int_equal(rc, -1);
}

static void fd_mpath_short(void **state)
{
	char buf[4];
	int rc;

	rc = __format_devname(buf, 1, sizeof(buf), "FOO");
	assert_int_equal(rc, -1);
}

static void fd_mpath_short1(void **state)
{
	char buf[5];
	int rc;

	rc = __format_devname(buf, 27, sizeof(buf), "FOO");
	assert_int_equal(rc, -1);
}

static int test_format_devname(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(fd_mpatha),
		cmocka_unit_test(fd_mpathz),
		cmocka_unit_test(fd_mpathaa),
		cmocka_unit_test(fd_mpathzz),
		cmocka_unit_test(fd_mpathaaa),
		cmocka_unit_test(fd_mpathzzz),
		cmocka_unit_test(fd_mpathaaaa),
		cmocka_unit_test(fd_mpathzzzz),
#ifdef MPATH_ID_INT_MAX
		cmocka_unit_test(fd_mpath_max),
#endif
		cmocka_unit_test(fd_mpath_max1),
		cmocka_unit_test(fd_mpath_short),
		cmocka_unit_test(fd_mpath_short1),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

static void sd_mpatha(void **state)
{
	int rc = scan_devname("MPATHa", "MPATH");

	assert_int_equal(rc, 1);
}

/*
 * Text after whitespace is ignored. But an overlong input
 * errors out, even if it's just whitespace.
 * It's kind of strange that scan_devname() treats whitespace
 * like this. But I'm not sure if some corner case depends
 * on this behavior.
 */
static void sd_mpatha_spc(void **state)
{
	int rc = scan_devname("MPATHa  00", "MPATH");

	assert_int_equal(rc, 1);
}

static void sd_mpatha_tab(void **state)
{
	int rc = scan_devname("MPATHa\t00", "MPATH");

	assert_int_equal(rc, 1);
}

static void sd_overlong(void **state)
{
	int rc = scan_devname("MPATHa       ", "MPATH");

	assert_int_equal(rc, -1);
}

static void sd_overlong1(void **state)
{
	int rc = scan_devname("MPATHabcdefgh", "MPATH");

	assert_int_equal(rc, -1);
}

static void sd_noprefix(void **state)
{
	int rc = scan_devname("MPATHa", NULL);

	assert_int_equal(rc, -1);
}

static void sd_nomatchprefix(void **state)
{
	int rc = scan_devname("MPATHa", "mpath");

	assert_int_equal(rc, -1);
}

static void sd_eq_prefix(void **state)
{
	int rc = scan_devname("MPATH", "MPATH");

	assert_int_equal(rc, -1);
}

static void sd_bad_1(void **state)
{
	int rc = scan_devname("MPATH0", "MPATH");

	assert_int_equal(rc, -1);
}

static void sd_bad_2(void **state)
{
	int rc = scan_devname("MPATHa0c", "MPATH");

	assert_int_equal(rc, -1);
}

#ifdef MPATH_ID_INT_MAX
static void sd_max(void **state)
{
	int rc = scan_devname("MPATH" MPATH_ID_INT_MAX, "MPATH");

	assert_int_equal(rc, INT_MAX);
}

static void sd_max_p1(void **state)
{
	int rc = scan_devname("MPATH" MPATH_ID_INT_MAX_p1, "MPATH");

	assert_int_equal(rc, -1);
}
#endif

static void sd_fd_many(void **state)
{
	char buf[32];
	int rc, i;

	for (i = 1; i < 5000; i++) {
		rc = __format_devname(buf, i, sizeof(buf), "MPATH");
		assert_in_range(rc, 6, 8);
		rc = scan_devname(buf, "MPATH");
		assert_int_equal(rc, i);
	}
}

static void sd_fd_random(void **state)
{
	char buf[32];
	int rc, i, n;

	srandom(1);
	for (i = 1; i < 1000; i++) {
		n = random() & 0xffff;
		rc = __format_devname(buf, n, sizeof(buf), "MPATH");
		assert_in_range(rc, 6, 9);
		rc = scan_devname(buf, "MPATH");
		assert_int_equal(rc, n);
	}
}

static int test_scan_devname(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(sd_mpatha),
		cmocka_unit_test(sd_mpatha_spc),
		cmocka_unit_test(sd_mpatha_tab),
		cmocka_unit_test(sd_overlong),
		cmocka_unit_test(sd_overlong1),
		cmocka_unit_test(sd_noprefix),
		cmocka_unit_test(sd_nomatchprefix),
		cmocka_unit_test(sd_eq_prefix),
		cmocka_unit_test(sd_bad_1),
		cmocka_unit_test(sd_bad_2),
#ifdef MPATH_ID_INT_MAX
		cmocka_unit_test(sd_max),
		cmocka_unit_test(sd_max_p1),
#endif
		cmocka_unit_test(sd_fd_many),
		cmocka_unit_test(sd_fd_random),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

static void mock_unused_alias(const char *alias)
{
	expect_string(__wrap_dm_map_present, str, alias);
	will_return(__wrap_dm_map_present, 0);
}

static void mock_self_alias(const char *alias, const char *wwid)
{
	expect_string(__wrap_dm_map_present, str, alias);
	will_return(__wrap_dm_map_present, 1);
	expect_string(__wrap_dm_get_uuid, name, alias);
	expect_value(__wrap_dm_get_uuid, uuid_len, WWID_SIZE);
	will_return(__wrap_dm_get_uuid, 0);
	will_return(__wrap_dm_get_uuid, wwid);
}

#define USED_STR(alias_str, wwid_str) wwid_str ": alias '" alias_str "' already taken, reselecting alias\n"
#define NOMATCH_STR(alias_str) ("No matching alias [" alias_str "] in bindings file.\n")
#define FOUND_STR(alias_str, wwid_str)				\
	"Found matching wwid [" wwid_str "] in bindings file."	\
	" Setting alias to " alias_str "\n"
#define FOUND_ALIAS_STR(alias_str, wwid_str)				\
	"Found matching alias [" alias_str "] in bindings file."	\
	" Setting wwid to " wwid_str "\n"
#define NOMATCH_WWID_STR(wwid_str) ("No matching wwid [" wwid_str "] in bindings file.\n")
#define NEW_STR(alias_str, wwid_str) ("Created new binding [" alias_str "] for WWID [" wwid_str "]\n")
#define EXISTING_STR(alias_str, wwid_str) ("Use existing binding [" alias_str "] for WWID [" wwid_str "]\n")
#define ALLOC_STR(alias_str, wwid_str) ("Allocated existing binding [" alias_str "] for WWID [" wwid_str "]\n")
#define BINDING_STR(alias_str, wwid_str) (alias_str " " wwid_str "\n")
#define BOUND_STR(alias_str, wwid_str) ("alias "alias_str " already bound to wwid " wwid_str ", cannot reuse")
#define ERR_STR(alias_str, wwid_str) ("ERROR: old alias [" alias_str "] for wwid [" wwid_str "] is used by other map\n")
#define REUSE_STR(alias_str, wwid_str) ("alias " alias_str " already bound to wwid " wwid_str ", cannot reuse\n")
#define NOMORE_STR "no more available user_friendly_names\n"

#define mock_failed_alias(alias, wwid)					\
	do {								\
		expect_string(__wrap_dm_map_present, str, alias);	\
		will_return(__wrap_dm_map_present, 1);			\
		expect_string(__wrap_dm_get_uuid, name, alias);		\
		expect_value(__wrap_dm_get_uuid, uuid_len, WWID_SIZE);	\
		will_return(__wrap_dm_get_uuid, 1);			\
		expect_condlog(3, USED_STR(alias, wwid));		\
	} while (0)

#define mock_used_alias(alias, wwid)					\
	do {								\
		expect_string(__wrap_dm_map_present, str, alias);	\
		will_return(__wrap_dm_map_present, 1);			\
		expect_string(__wrap_dm_get_uuid, name, alias);		\
		expect_value(__wrap_dm_get_uuid, uuid_len, WWID_SIZE);	\
		will_return(__wrap_dm_get_uuid, 0);			\
		will_return(__wrap_dm_get_uuid, "WWID_USED");		\
		expect_condlog(3, USED_STR(alias, wwid));		\
	} while(0)

static void mock_bindings_file(const char *content, int match_line)
{
	static char cnt[1024];
	char *token;
	int i;

	assert_in_range(strlcpy(cnt, content, sizeof(cnt)), 0, sizeof(cnt) - 1);

	for (token = strtok(cnt, "\n"), i = 0;
	     token && *token;
	     token = strtok(NULL, "\n"), i++) {
		will_return(__wrap_fgets, token);
		if (match_line == i)
			return;
	}
	will_return(__wrap_fgets, NULL);
}

static void lb_empty(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("", -1);
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));
	rc = lookup_binding(NULL, "WWID0", &alias, NULL, 0);
	assert_int_equal(rc, 1);
	assert_ptr_equal(alias, NULL);
}

static void lb_empty_unused(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("", -1);
	mock_unused_alias("MPATHa");
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));
	rc = lookup_binding(NULL, "WWID0", &alias, "MPATH", 1);
	assert_int_equal(rc, 1);
	assert_ptr_equal(alias, NULL);
	free(alias);
}

static void lb_empty_failed(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("", -1);
	mock_failed_alias("MPATHa", "WWID0");
	mock_unused_alias("MPATHb");
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));
	rc = lookup_binding(NULL, "WWID0", &alias, "MPATH", 1);
	assert_int_equal(rc, 2);
	assert_ptr_equal(alias, NULL);
	free(alias);
}

static void lb_empty_1_used(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("", -1);
	mock_used_alias("MPATHa", "WWID0");
	mock_unused_alias("MPATHb");
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));
	rc = lookup_binding(NULL, "WWID0", &alias, "MPATH", 1);
	assert_int_equal(rc, 2);
	assert_ptr_equal(alias, NULL);
	free(alias);
}

static void lb_empty_1_used_self(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("", -1);
	mock_used_alias("MPATHa", "WWID0");
	mock_self_alias("MPATHb", "WWID0");
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));
	rc = lookup_binding(NULL, "WWID0", &alias, "MPATH", 1);
	assert_int_equal(rc, 2);
	assert_ptr_equal(alias, NULL);
	free(alias);
}

static void lb_match_a(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n", 0);
	expect_condlog(3, FOUND_STR("MPATHa", "WWID0"));
	rc = lookup_binding(NULL, "WWID0", &alias, "MPATH", 0);
	assert_int_equal(rc, 0);
	assert_ptr_not_equal(alias, NULL);
	assert_string_equal(alias, "MPATHa");
	free(alias);
}

static void lb_nomatch_a(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n", -1);
	expect_condlog(3, NOMATCH_WWID_STR("WWID1"));
	rc = lookup_binding(NULL, "WWID1", &alias, "MPATH", 0);
	assert_int_equal(rc, 2);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_a_bad_check(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n", -1);
	expect_condlog(0, NOMORE_STR);
	rc = lookup_binding(NULL, "WWID1", &alias, NULL, 1);
	assert_int_equal(rc, -1);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_a_unused(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n", -1);
	mock_unused_alias("MPATHb");
	expect_condlog(3, NOMATCH_WWID_STR("WWID1"));
	rc = lookup_binding(NULL, "WWID1", &alias, "MPATH", 1);
	assert_int_equal(rc, 2);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_a_3_used_failed_self(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n", -1);
	mock_used_alias("MPATHb", "WWID1");
	mock_used_alias("MPATHc", "WWID1");
	mock_used_alias("MPATHd", "WWID1");
	mock_failed_alias("MPATHe", "WWID1");
	mock_self_alias("MPATHf", "WWID1");
	expect_condlog(3, NOMATCH_WWID_STR("WWID1"));
	rc = lookup_binding(NULL, "WWID1", &alias, "MPATH", 1);
	assert_int_equal(rc, 6);
	assert_ptr_equal(alias, NULL);
}

static void do_lb_match_c(void **state, int check_if_taken)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n"
			   "MPATHc WWID1", 1);
	expect_condlog(3, FOUND_STR("MPATHc", "WWID1"));
	rc = lookup_binding(NULL, "WWID1", &alias, "MPATH", check_if_taken);
	assert_int_equal(rc, 0);
	assert_ptr_not_equal(alias, NULL);
	assert_string_equal(alias, "MPATHc");
	free(alias);
}

static void lb_match_c(void **state)
{
	do_lb_match_c(state, 0);
}

static void lb_match_c_check(void **state)
{
	do_lb_match_c(state, 1);
}

static void lb_nomatch_a_c(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n"
			   "MPATHc WWID1", -1);
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 0);
	assert_int_equal(rc, 2);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_a_d_unused(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n"
			   "MPATHd WWID1", -1);
	mock_unused_alias("MPATHb");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, 2);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_a_d_1_used(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n"
			   "MPATHd WWID1", -1);
	mock_used_alias("MPATHb", "WWID2");
	mock_unused_alias("MPATHc");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, 3);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_a_d_2_used(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n"
			   "MPATHd WWID1", -1);
	mock_used_alias("MPATHb", "WWID2");
	mock_used_alias("MPATHc", "WWID2");
	mock_unused_alias("MPATHe");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, 5);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_a_d_3_used(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n"
			   "MPATHd WWID1", -1);
	mock_used_alias("MPATHb", "WWID2");
	mock_used_alias("MPATHc", "WWID2");
	mock_used_alias("MPATHe", "WWID2");
	mock_unused_alias("MPATHf");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, 6);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_c_a(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHc WWID1\n"
			   "MPATHa WWID0\n", -1);
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 0);
	assert_int_equal(rc, 2);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_d_a_unused(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHc WWID1\n"
			   "MPATHa WWID0\n"
			   "MPATHd WWID0\n", -1);
	mock_unused_alias("MPATHb");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, 2);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_d_a_1_used(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHc WWID1\n"
			   "MPATHa WWID0\n"
			   "MPATHd WWID0\n", -1);
	mock_used_alias("MPATHb", "WWID2");
	mock_unused_alias("MPATHe");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, 5);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_a_b(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n"
			   "MPATHz WWID26\n"
			   "MPATHb WWID1\n", -1);
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 0);
	assert_int_equal(rc, 3);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_a_b_bad(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n"
			   "MPATHz WWID26\n"
			   "MPATHb\n", -1);
	expect_condlog(3, "Ignoring malformed line 3 in bindings file\n");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 0);
	assert_int_equal(rc, 3);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_a_b_bad_self(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n"
			   "MPATHz WWID26\n"
			   "MPATHb\n", -1);
	expect_condlog(3, "Ignoring malformed line 3 in bindings file\n");
	mock_self_alias("MPATHc", "WWID2");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, 3);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_b_a(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHb WWID1\n"
			   "MPATHz WWID26\n"
			   "MPATHa WWID0\n", -1);
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 0);
	assert_int_equal(rc, 27);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_b_a_3_used(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHb WWID1\n"
			   "MPATHz WWID26\n"
			   "MPATHa WWID0\n", -1);
	mock_used_alias("MPATHaa", "WWID2");
	mock_used_alias("MPATHab", "WWID2");
	mock_used_alias("MPATHac", "WWID2");
	mock_unused_alias("MPATHad");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, 30);
	assert_ptr_equal(alias, NULL);
}

#ifdef MPATH_ID_INT_MAX
static void do_lb_nomatch_int_max(void **state, int check_if_taken)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHb WWID1\n"
			   "MPATH" MPATH_ID_INT_MAX " WWIDMAX\n"
			   "MPATHa WWID0\n", -1);
	expect_condlog(0, NOMORE_STR);
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", check_if_taken);
	assert_int_equal(rc, -1);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_int_max(void **state)
{
	do_lb_nomatch_int_max(state, 0);
}

static void lb_nomatch_int_max_check(void **state)
{
	do_lb_nomatch_int_max(state, 1);
}

static void lb_nomatch_int_max_used(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHb WWID1\n"
			   "MPATH" MPATH_ID_INT_MAX " WWIDMAX\n", -1);
	mock_used_alias("MPATHa", "WWID2");
	expect_condlog(0, NOMORE_STR);
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, -1);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_int_max_m1(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHb WWID1\n"
			   "MPATH" MPATH_ID_INT_MAX_m1 " WWIDMAX\n"
			   "MPATHa WWID0\n", -1);
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 0);
	assert_int_equal(rc, INT_MAX);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_int_max_m1_used(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHb WWID1\n"
			   "MPATH" MPATH_ID_INT_MAX_m1 " WWIDMAX\n"
			   "MPATHa WWID0\n", -1);
	mock_used_alias("MPATH" MPATH_ID_INT_MAX, "WWID2");
	expect_condlog(0, NOMORE_STR);
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, -1);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_int_max_m1_1_used(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHb WWID1\n"
			   "MPATH" MPATH_ID_INT_MAX_m1 " WWIDMAX\n", -1);
	mock_used_alias("MPATHa", "WWID2");
	mock_unused_alias("MPATH" MPATH_ID_INT_MAX);
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, INT_MAX);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_int_max_m1_2_used(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHb WWID1\n"
			   "MPATH" MPATH_ID_INT_MAX_m1 " WWIDMAX\n", -1);
	mock_used_alias("MPATHa", "WWID2");
	mock_used_alias("MPATH" MPATH_ID_INT_MAX, "WWID2");
	expect_condlog(0, NOMORE_STR);
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, -1);
	assert_ptr_equal(alias, NULL);
}
#endif

static int test_lookup_binding(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(lb_empty),
		cmocka_unit_test(lb_empty_unused),
		cmocka_unit_test(lb_empty_failed),
		cmocka_unit_test(lb_empty_1_used),
		cmocka_unit_test(lb_empty_1_used_self),
		cmocka_unit_test(lb_match_a),
		cmocka_unit_test(lb_nomatch_a),
		cmocka_unit_test(lb_nomatch_a_bad_check),
		cmocka_unit_test(lb_nomatch_a_unused),
		cmocka_unit_test(lb_nomatch_a_3_used_failed_self),
		cmocka_unit_test(lb_match_c),
		cmocka_unit_test(lb_match_c_check),
		cmocka_unit_test(lb_nomatch_a_c),
		cmocka_unit_test(lb_nomatch_a_d_unused),
		cmocka_unit_test(lb_nomatch_a_d_1_used),
		cmocka_unit_test(lb_nomatch_a_d_2_used),
		cmocka_unit_test(lb_nomatch_a_d_3_used),
		cmocka_unit_test(lb_nomatch_c_a),
		cmocka_unit_test(lb_nomatch_d_a_unused),
		cmocka_unit_test(lb_nomatch_d_a_1_used),
		cmocka_unit_test(lb_nomatch_a_b),
		cmocka_unit_test(lb_nomatch_a_b_bad),
		cmocka_unit_test(lb_nomatch_a_b_bad_self),
		cmocka_unit_test(lb_nomatch_b_a),
		cmocka_unit_test(lb_nomatch_b_a_3_used),
#ifdef MPATH_ID_INT_MAX
		cmocka_unit_test(lb_nomatch_int_max),
		cmocka_unit_test(lb_nomatch_int_max_check),
		cmocka_unit_test(lb_nomatch_int_max_used),
		cmocka_unit_test(lb_nomatch_int_max_m1),
		cmocka_unit_test(lb_nomatch_int_max_m1_used),
		cmocka_unit_test(lb_nomatch_int_max_m1_1_used),
		cmocka_unit_test(lb_nomatch_int_max_m1_2_used),
#endif
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

static void rl_empty(void **state)
{
	int rc;
	char buf[WWID_SIZE];

	buf[0] = '\0';
	mock_bindings_file("", -1);
	expect_condlog(3, NOMATCH_STR("MPATHa"));
	rc = rlookup_binding(NULL, buf, "MPATHa");
	assert_int_equal(rc, -1);
	assert_string_equal(buf, "");
}

static void rl_match_a(void **state)
{
	int rc;
	char buf[WWID_SIZE];

	buf[0] = '\0';
	mock_bindings_file("MPATHa WWID0\n", 0);
	expect_condlog(3, FOUND_ALIAS_STR("MPATHa", "WWID0"));
	rc = rlookup_binding(NULL, buf, "MPATHa");
	assert_int_equal(rc, 0);
	assert_string_equal(buf, "WWID0");
}

static void rl_nomatch_a(void **state)
{
	int rc;
	char buf[WWID_SIZE];

	buf[0] = '\0';
	mock_bindings_file("MPATHa WWID0\n", -1);
	expect_condlog(3, NOMATCH_STR("MPATHb"));
	rc = rlookup_binding(NULL, buf, "MPATHb");
	assert_int_equal(rc, -1);
	assert_string_equal(buf, "");
}

static void rl_malformed_a(void **state)
{
	int rc;
	char buf[WWID_SIZE];

	buf[0] = '\0';
	mock_bindings_file("MPATHa     \n", -1);
	expect_condlog(3, "Ignoring malformed line 1 in bindings file\n");
	expect_condlog(3, NOMATCH_STR("MPATHa"));
	rc = rlookup_binding(NULL, buf, "MPATHa");
	assert_int_equal(rc, -1);
	assert_string_equal(buf, "");
}

static void rl_overlong_a(void **state)
{
	int rc;
	char buf[WWID_SIZE];
	char line[WWID_SIZE + 10];

	snprintf(line, sizeof(line), "MPATHa ");
	memset(line + strlen(line), 'W', sizeof(line) - 2 - strlen(line));
	snprintf(line + sizeof(line) - 2, 2, "\n");

	buf[0] = '\0';
	mock_bindings_file(line, -1);
	expect_condlog(3, "Ignoring too large wwid at 1 in bindings file\n");
	expect_condlog(3, NOMATCH_STR("MPATHa"));
	rc = rlookup_binding(NULL, buf, "MPATHa");
	assert_int_equal(rc, -1);
	assert_string_equal(buf, "");
}

static void rl_match_b(void **state)
{
	int rc;
	char buf[WWID_SIZE];

	buf[0] = '\0';
	mock_bindings_file("MPATHa WWID0\n"
			   "MPATHz WWID26\n"
			   "MPATHb WWID2\n", 2);
	expect_condlog(3, FOUND_ALIAS_STR("MPATHb", "WWID2"));
	rc = rlookup_binding(NULL, buf, "MPATHb");
	assert_int_equal(rc, 0);
	assert_string_equal(buf, "WWID2");
}

static int test_rlookup_binding(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(rl_empty),
		cmocka_unit_test(rl_match_a),
		cmocka_unit_test(rl_nomatch_a),
		cmocka_unit_test(rl_malformed_a),
		cmocka_unit_test(rl_overlong_a),
		cmocka_unit_test(rl_match_b),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

static void al_a(void **state)
{
	static const char ln[] = "MPATHa WWIDa\n";
	char *alias;

	will_return(__wrap_lseek, 0);
	expect_value(__wrap_write, count, strlen(ln));
	expect_string(__wrap_write, buf, ln);
	will_return(__wrap_write, strlen(ln));
	expect_condlog(3, NEW_STR("MPATHa", "WWIDa"));

	alias = allocate_binding(0, "WWIDa", 1, "MPATH");
	assert_ptr_not_equal(alias, NULL);
	assert_string_equal(alias, "MPATHa");
	free(alias);
}

static void al_zz(void **state)
{
	static const char ln[] = "MPATHzz WWIDzz\n";
	char *alias;

	will_return(__wrap_lseek, 0);
	expect_value(__wrap_write, count, strlen(ln));
	expect_string(__wrap_write, buf, ln);
	will_return(__wrap_write, strlen(ln));
	expect_condlog(3, NEW_STR("MPATHzz", "WWIDzz"));

	alias = allocate_binding(0, "WWIDzz", 26*26 + 26, "MPATH");
	assert_ptr_not_equal(alias, NULL);
	assert_string_equal(alias, "MPATHzz");
	free(alias);
}

static void al_0(void **state)
{
	char *alias;

	expect_condlog(0, "allocate_binding: cannot allocate new binding for id 0\n");
	alias = allocate_binding(0, "WWIDa", 0, "MPATH");
	assert_ptr_equal(alias, NULL);
}

static void al_m2(void **state)
{
	char *alias;

	expect_condlog(0, "allocate_binding: cannot allocate new binding for id -2\n");
	alias = allocate_binding(0, "WWIDa", -2, "MPATH");
	assert_ptr_equal(alias, NULL);
}

static void al_lseek_err(void **state)
{
	char *alias;

	will_return(__wrap_lseek, -ENODEV);
	expect_condlog(0, "Cannot seek to end of bindings file : No such device\n");
	alias = allocate_binding(0, "WWIDa", 1, "MPATH");
	assert_ptr_equal(alias, NULL);
}

static void al_write_err(void **state)
{
	static const char ln[] = "MPATHa WWIDa\n";
	const int offset = 20;
	char *alias;

	will_return(__wrap_lseek, offset);
	expect_value(__wrap_write, count, strlen(ln));
	expect_string(__wrap_write, buf, ln);
	will_return(__wrap_write, strlen(ln) - 1);
	expect_value(__wrap_ftruncate, length, offset);
	will_return(__wrap_ftruncate, 0);
	expect_condlog(0, "Cannot write binding to bindings file :");

	alias = allocate_binding(0, "WWIDa", 1, "MPATH");
	assert_ptr_equal(alias, NULL);
}

static int test_allocate_binding(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(al_a),
		cmocka_unit_test(al_zz),
		cmocka_unit_test(al_0),
		cmocka_unit_test(al_m2),
		cmocka_unit_test(al_lseek_err),
		cmocka_unit_test(al_write_err),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

#define mock_allocate_binding(alias, wwid)				\
	do {								\
		static const char ln[] = BINDING_STR(alias, wwid);	\
									\
		will_return(__wrap_lseek, 0);				\
		expect_value(__wrap_write, count, strlen(ln));		\
		expect_string(__wrap_write, buf, ln);			\
		will_return(__wrap_write, strlen(ln));			\
		expect_condlog(3, NEW_STR(alias, wwid));		\
	} while (0)

static void gufa_empty_new_rw(void **state) {
	char *alias;

	will_return(__wrap_open_file, true);

	mock_bindings_file("", -1);
	mock_unused_alias("MPATHa");
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));

	mock_allocate_binding("MPATHa", "WWID0");
	alias = get_user_friendly_alias("WWID0", "x", "", "MPATH", false);
	assert_string_equal(alias, "MPATHa");
	free(alias);
}

static void gufa_empty_new_ro_1(void **state) {
	char *alias;
	will_return(__wrap_open_file, false);
	mock_bindings_file("", -1);
	mock_unused_alias("MPATHa");
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));

	alias = get_user_friendly_alias("WWID0", "x", "", "MPATH", false);
	assert_ptr_equal(alias, NULL);
}

static void gufa_empty_new_ro_2(void **state) {
	char *alias;

	will_return(__wrap_open_file, true);

	mock_bindings_file("", -1);
	mock_unused_alias("MPATHa");
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));

	alias = get_user_friendly_alias("WWID0", "x", "", "MPATH", true);
	assert_ptr_equal(alias, NULL);
}

static void gufa_match_a_unused(void **state) {
	char *alias;

	will_return(__wrap_open_file, true);

	mock_bindings_file("MPATHa WWID0", 0);
	expect_condlog(3, FOUND_STR("MPATHa", "WWID0"));
	mock_unused_alias("MPATHa");

	alias = get_user_friendly_alias("WWID0", "x", "", "MPATH", true);
	assert_string_equal(alias, "MPATHa");
	free(alias);
}

static void gufa_match_a_self(void **state) {
	char *alias;

	will_return(__wrap_open_file, true);

	mock_bindings_file("MPATHa WWID0", 0);
	expect_condlog(3, FOUND_STR("MPATHa", "WWID0"));
	mock_self_alias("MPATHa", "WWID0");

	alias = get_user_friendly_alias("WWID0", "x", "", "MPATH", true);
	assert_string_equal(alias, "MPATHa");
	free(alias);
}

static void gufa_match_a_used(void **state) {
	char *alias;

	will_return(__wrap_open_file, true);

	mock_bindings_file("MPATHa WWID0", 0);
	expect_condlog(3, FOUND_STR("MPATHa", "WWID0"));
	mock_used_alias("MPATHa", "WWID0");

	alias = get_user_friendly_alias("WWID0", "x", "", "MPATH", true);
	assert_ptr_equal(alias, NULL);
}

static void gufa_nomatch_a_c(void **state) {
	char *alias;
	will_return(__wrap_open_file, true);

	mock_bindings_file("MPATHa WWID0\n"
			   "MPATHc WWID2",
			   -1);
	mock_unused_alias("MPATHb");
	expect_condlog(3, NOMATCH_WWID_STR("WWID1"));

	mock_allocate_binding("MPATHb", "WWID1");

	alias = get_user_friendly_alias("WWID1", "x", "", "MPATH", false);
	assert_string_equal(alias, "MPATHb");
	free(alias);
}

static void gufa_nomatch_c_a(void **state) {
	char *alias;
	will_return(__wrap_open_file, true);

	mock_bindings_file("MPATHc WWID2\n"
			   "MPATHa WWID0",
			   -1);
	mock_unused_alias("MPATHb");
	expect_condlog(3, NOMATCH_WWID_STR("WWID1"));

	mock_allocate_binding("MPATHb", "WWID1");

	alias = get_user_friendly_alias("WWID1", "x", "", "MPATH", false);
	assert_string_equal(alias, "MPATHb");
	free(alias);
}

static void gufa_nomatch_c_b(void **state) {
	char *alias;
	will_return(__wrap_open_file, true);

	mock_bindings_file("MPATHc WWID2\n"
			   "MPATHb WWID1\n",
			   -1);
	mock_unused_alias("MPATHa");
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));

	mock_allocate_binding("MPATHa", "WWID0");

	alias = get_user_friendly_alias("WWID0", "x", "", "MPATH", false);
	assert_string_equal(alias, "MPATHa");
	free(alias);
}

static void gufa_nomatch_c_b_used(void **state) {
	char *alias;
	will_return(__wrap_open_file, true);

	mock_bindings_file("MPATHc WWID2\n"
			   "MPATHb WWID1",
			   -1);
	mock_used_alias("MPATHa", "WWID4");
	expect_condlog(3, NOMATCH_WWID_STR("WWID4"));
	mock_unused_alias("MPATHd");

	mock_allocate_binding("MPATHd", "WWID4");

	alias = get_user_friendly_alias("WWID4", "x", "", "MPATH", false);
	assert_string_equal(alias, "MPATHd");
	free(alias);
}

static void gufa_nomatch_b_f_a(void **state) {
	char *alias;
	will_return(__wrap_open_file, true);

	mock_bindings_file("MPATHb WWID1\n"
			   "MPATHf WWID6\n"
			   "MPATHa WWID0\n",
			   -1);
	expect_condlog(3, NOMATCH_WWID_STR("WWID7"));
	mock_unused_alias("MPATHg");

	mock_allocate_binding("MPATHg", "WWID7");

	alias = get_user_friendly_alias("WWID7", "x", "", "MPATH", false);
	assert_string_equal(alias, "MPATHg");
	free(alias);
}

static void gufa_old_empty(void **state) {
	char *alias;
	will_return(__wrap_open_file, true);

	/* rlookup_binding for ALIAS */
	mock_bindings_file("", -1);
	expect_condlog(3, NOMATCH_STR("MPATHz"));

	/* lookup_binding */
	mock_bindings_file("", -1);
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));

	mock_allocate_binding("MPATHz", "WWID0");
	expect_condlog(2, ALLOC_STR("MPATHz", "WWID0"));

	alias = get_user_friendly_alias("WWID0", "x", "MPATHz", "MPATH", false);
	assert_string_equal(alias, "MPATHz");
	free(alias);
}

static void gufa_old_match(void **state) {
	char *alias;
	will_return(__wrap_open_file, true);

	mock_bindings_file("MPATHb WWID1\n"
			   "MPATHz WWID0",
			   1);
	expect_condlog(3, FOUND_ALIAS_STR("MPATHz", "WWID0"));

	alias = get_user_friendly_alias("WWID0", "x", "MPATHz", "MPATH", false);
	assert_string_equal(alias, "MPATHz");
	free(alias);
}

static void gufa_old_match_other(void **state) {
	char *alias;
	static const char bindings[] = "MPATHz WWID9";

	will_return(__wrap_open_file, true);

	mock_bindings_file(bindings, 0);
	expect_condlog(3, FOUND_ALIAS_STR("MPATHz", "WWID9"));
	expect_condlog(0, REUSE_STR("MPATHz", "WWID9"));

	mock_bindings_file(bindings, -1);
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));
	mock_unused_alias("MPATHa");

	mock_allocate_binding("MPATHa", "WWID0");

	alias = get_user_friendly_alias("WWID0", "x", "MPATHz", "MPATH", false);
	assert_string_equal(alias, "MPATHa");
	free(alias);
}

static void gufa_old_match_other_used(void **state) {
	char *alias;
	static const char bindings[] = "MPATHz WWID9";

	will_return(__wrap_open_file, true);

	mock_bindings_file(bindings, 0);
	expect_condlog(3, FOUND_ALIAS_STR("MPATHz", "WWID9"));
	expect_condlog(0, REUSE_STR("MPATHz", "WWID9"));

	mock_bindings_file(bindings, -1);
	mock_used_alias("MPATHa", "WWID0");
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));
	mock_unused_alias("MPATHb");

	mock_allocate_binding("MPATHb", "WWID0");

	alias = get_user_friendly_alias("WWID0", "x", "MPATHz", "MPATH", false);
	assert_string_equal(alias, "MPATHb");
	free(alias);
}

static void gufa_old_match_other_wwidmatch(void **state) {
	char *alias;
	static const char bindings[] = ("MPATHz WWID9\n"
					"MPATHc WWID2");
	will_return(__wrap_open_file, true);

	mock_bindings_file(bindings, 0);
	expect_condlog(3, FOUND_ALIAS_STR("MPATHz", "WWID9"));
	expect_condlog(0, REUSE_STR("MPATHz", "WWID9"));

	mock_bindings_file(bindings, 1);
	expect_condlog(3, FOUND_STR("MPATHc", "WWID2"));
	mock_unused_alias("MPATHc");

	alias = get_user_friendly_alias("WWID2", "x", "MPATHz", "MPATH", false);
	assert_string_equal(alias, "MPATHc");
	free(alias);
}

static void gufa_old_match_other_wwidmatch_used(void **state) {
	char *alias;
	static const char bindings[] = ("MPATHz WWID9\n"
					"MPATHc WWID2");

	will_return(__wrap_open_file, true);

	mock_bindings_file(bindings, 0);
	expect_condlog(3, FOUND_ALIAS_STR("MPATHz", "WWID9"));
	expect_condlog(0, REUSE_STR("MPATHz", "WWID9"));

	mock_bindings_file(bindings, 1);
	expect_condlog(3, FOUND_STR("MPATHc", "WWID2"));
	mock_used_alias("MPATHc", "WWID2");

	alias = get_user_friendly_alias("WWID2", "x", "MPATHz", "MPATH", false);
	assert_ptr_equal(alias, NULL);
}

static void gufa_old_nomatch_wwidmatch(void **state) {
	char *alias;
	static const char bindings[] = "MPATHa WWID0";

	will_return(__wrap_open_file, true);

	mock_bindings_file(bindings, -1);
	expect_condlog(3, NOMATCH_STR("MPATHz"));

	mock_bindings_file(bindings, 0);
	expect_condlog(3, FOUND_STR("MPATHa", "WWID0"));
	mock_unused_alias("MPATHa");
	expect_condlog(3, EXISTING_STR("MPATHa", "WWID0"));

	alias = get_user_friendly_alias("WWID0", "x", "MPATHz", "MPATH", false);
	assert_string_equal(alias, "MPATHa");
	free(alias);
}

static void gufa_old_nomatch_wwidmatch_used(void **state) {
	char *alias;
	static const char bindings[] = "MPATHa WWID0";
	will_return(__wrap_open_file, true);

	mock_bindings_file(bindings, -1);
	expect_condlog(3, NOMATCH_STR("MPATHz"));

	mock_bindings_file(bindings, 0);
	expect_condlog(3, FOUND_STR("MPATHa", "WWID0"));
	mock_used_alias("MPATHa", "WWID0");

	alias = get_user_friendly_alias("WWID0", "x", "MPATHz", "MPATH", false);
	assert_ptr_equal(alias, NULL);
}

static void gufa_old_nomatch_nowwidmatch(void **state) {
	char *alias;
	static const char bindings[] = "MPATHb WWID1";

	will_return(__wrap_open_file, true);

	mock_bindings_file(bindings, -1);
	expect_condlog(3, NOMATCH_STR("MPATHz"));

	mock_bindings_file(bindings, -1);
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));

	mock_allocate_binding("MPATHz", "WWID0");
	expect_condlog(2, ALLOC_STR("MPATHz", "WWID0"));

	alias = get_user_friendly_alias("WWID0", "x", "MPATHz", "MPATH", false);
	assert_string_equal(alias, "MPATHz");
	free(alias);
}

static int test_get_user_friendly_alias()
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(gufa_empty_new_rw),
		cmocka_unit_test(gufa_empty_new_ro_1),
		cmocka_unit_test(gufa_empty_new_ro_2),
		cmocka_unit_test(gufa_match_a_unused),
		cmocka_unit_test(gufa_match_a_self),
		cmocka_unit_test(gufa_match_a_used),
		cmocka_unit_test(gufa_nomatch_a_c),
		cmocka_unit_test(gufa_nomatch_c_a),
		cmocka_unit_test(gufa_nomatch_c_b),
		cmocka_unit_test(gufa_nomatch_c_b_used),
		cmocka_unit_test(gufa_nomatch_b_f_a),
		cmocka_unit_test(gufa_old_empty),
		cmocka_unit_test(gufa_old_match),
		cmocka_unit_test(gufa_old_match_other),
		cmocka_unit_test(gufa_old_match_other_used),
		cmocka_unit_test(gufa_old_match_other_wwidmatch),
		cmocka_unit_test(gufa_old_match_other_wwidmatch_used),
		cmocka_unit_test(gufa_old_nomatch_wwidmatch),
		cmocka_unit_test(gufa_old_nomatch_wwidmatch_used),
		cmocka_unit_test(gufa_old_nomatch_nowwidmatch),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

int main(void)
{
	int ret = 0;
	init_test_verbosity(3);

	ret += test_format_devname();
	ret += test_scan_devname();
	ret += test_lookup_binding();
	ret += test_rlookup_binding();
	ret += test_allocate_binding();
	ret += test_allocate_binding();
	ret += test_get_user_friendly_alias();

	return ret;
}
