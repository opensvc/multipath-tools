#include <pthread.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <cmocka.h>
#include "strbuf.h"
#include "util.h"
#include "alias.h"
#include "test-log.h"
#include <errno.h>
#include <string.h>

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

/*
 * allocate_binding -> write_bindings_file() writes the entire file, i.e. the
 * header, any pre-existing bindings, and the new binding. The complete content
 * depends on history and is different to predict here. Therefore we check only
 * the newly added binding. Because add_binding() sorts entries, this new
 * binding isn't necessarily the last one; receive it from will_return() and
 * search for it with strstr().
 * If the string to be written doesn't start with the bindings file
 * header, it's a test of a partial write.
 */
ssize_t __wrap_write(int fd, const void *buf, size_t count)
{
	const char *binding, *start;

#if DEBUG_WRITE
	fprintf(stderr, "%s: %zx exp %zx\n===\n%s\n===\n", __func__, strlen(buf),
		count, (const char *)buf);
#endif
	if (!strncmp((const char *)buf, BINDINGS_FILE_HEADER,
		     sizeof(BINDINGS_FILE_HEADER) - 1))
		start = (const char *)buf + sizeof(BINDINGS_FILE_HEADER) - 1;
	else
		start = buf;
	binding = mock_ptr_type(char *);
	start = strstr(start, binding);
	check_expected(count);
	assert_ptr_not_equal(start, NULL);
	return __set_errno(mock_type(int));
}

int __wrap_rename(const char *old, const char *new)
{
	return __set_errno(mock_type(int));
}

int __wrap_mkstemp(char *template)
{
	return 10;
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
	expect_string(__wrap_dm_get_uuid, name, alias);
	expect_value(__wrap_dm_get_uuid, uuid_len, WWID_SIZE);
	will_return(__wrap_dm_get_uuid, 1);
}

static void mock_self_alias(const char *alias, const char *wwid)
{
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
		expect_string(__wrap_dm_get_uuid, name, alias);		\
		expect_value(__wrap_dm_get_uuid, uuid_len, WWID_SIZE);	\
		will_return(__wrap_dm_get_uuid, 1);			\
	} while (0)

#define mock_used_alias(alias, wwid)					\
	do {								\
		expect_string(__wrap_dm_get_uuid, name, alias);		\
		expect_value(__wrap_dm_get_uuid, uuid_len, WWID_SIZE);	\
		will_return(__wrap_dm_get_uuid, 0);			\
		will_return(__wrap_dm_get_uuid, "WWID_USED");		\
		expect_condlog(3, USED_STR(alias, wwid));		\
	} while(0)

static void __mock_bindings_file(const char *content)
{
	char *cnt __attribute__((cleanup(cleanup_charp))) = NULL;
	char *token, *savep = NULL;
	int i;

	cnt = strdup(content);
	assert_ptr_not_equal(cnt, NULL);

	for (token = strtok_r(cnt, "\n", &savep), i = 0;
	     token && *token;
	     token = strtok_r(NULL, "\n", &savep), i++) {
		char *alias, *wwid;
		int rc;

		if (read_binding(token, i + 1, &alias, &wwid)
		    == READ_BINDING_SKIP)
			continue;

		rc = add_binding(&global_bindings, alias, wwid);
		assert_int_equal(rc, BINDING_ADDED);
	}
}

static void mock_bindings_file(const char *content) {
	return __mock_bindings_file(content);
}

static int teardown_bindings(void **state)
{
	cleanup_bindings();
	return 0;
}

static int lookup_binding(FILE *dummy, const char *wwid, char **alias,
			  const char *prefix, int check_if_taken)
{
	const struct binding *bdg;
	int id;

	/*
	 * get_free_id() always checks if aliases are taken.
	 * Therefore if prefix is non-null, check_if_taken must be true.
	 */
	assert_true(!prefix || check_if_taken);
	*alias = NULL;
	bdg = get_binding_for_wwid(&global_bindings, wwid);
	if (bdg) {
		*alias = strdup(bdg->alias);
		return 0;
	} else if (!prefix && check_if_taken)
		return -1;

	id = get_free_id(&global_bindings, prefix, wwid);
	return id;
}

static void lb_empty(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("");
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));
	rc = lookup_binding(NULL, "WWID0", &alias, NULL, 0);
	assert_int_equal(rc, 1);
	assert_ptr_equal(alias, NULL);
}

static void lb_empty_unused(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("");
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

	mock_bindings_file("");
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));
	mock_failed_alias("MPATHa", "WWID0");
	rc = lookup_binding(NULL, "WWID0", &alias, "MPATH", 1);
	assert_int_equal(rc, 1);
	assert_ptr_equal(alias, NULL);
	free(alias);
}

static void lb_empty_1_used(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("");
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));
	mock_used_alias("MPATHa", "WWID0");
	mock_unused_alias("MPATHb");
	rc = lookup_binding(NULL, "WWID0", &alias, "MPATH", 1);
	assert_int_equal(rc, 2);
	assert_ptr_equal(alias, NULL);
	free(alias);
}

static void lb_empty_1_used_self(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("");
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));
	mock_used_alias("MPATHa", "WWID0");
	mock_self_alias("MPATHb", "WWID0");
	rc = lookup_binding(NULL, "WWID0", &alias, "MPATH", 1);
	assert_int_equal(rc, 2);
	assert_ptr_equal(alias, NULL);
	free(alias);
}

static void lb_match_a(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n");
	expect_condlog(3, FOUND_STR("MPATHa", "WWID0"));
	rc = lookup_binding(NULL, "WWID0", &alias, "MPATH", 1);
	assert_int_equal(rc, 0);
	assert_ptr_not_equal(alias, NULL);
	assert_string_equal(alias, "MPATHa");
	free(alias);
}

static void lb_nomatch_a(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n");
	expect_condlog(3, NOMATCH_WWID_STR("WWID1"));
	mock_unused_alias("MPATHb");
	rc = lookup_binding(NULL, "WWID1", &alias, "MPATH", 1);
	assert_int_equal(rc, 2);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_a_bad_check(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n");
	expect_condlog(3, NOMATCH_WWID_STR("WWID1"));
	rc = lookup_binding(NULL, "WWID1", &alias, NULL, 1);
	assert_int_equal(rc, -1);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_a_unused(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n");
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

	mock_bindings_file("MPATHa WWID0\n");
	expect_condlog(3, NOMATCH_WWID_STR("WWID1"));
	mock_used_alias("MPATHb", "WWID1");
	mock_used_alias("MPATHc", "WWID1");
	mock_used_alias("MPATHd", "WWID1");
	mock_failed_alias("MPATHe", "WWID1");
	rc = lookup_binding(NULL, "WWID1", &alias, "MPATH", 1);
	assert_int_equal(rc, 5);
	assert_ptr_equal(alias, NULL);
}

static void do_lb_match_c(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n"
			   "MPATHc WWID1");
	expect_condlog(3, FOUND_STR("MPATHc", "WWID1"));
	rc = lookup_binding(NULL, "WWID1", &alias, "MPATH", 1);
	assert_int_equal(rc, 0);
	assert_ptr_not_equal(alias, NULL);
	assert_string_equal(alias, "MPATHc");
	free(alias);
}

static void lb_match_c(void **state)
{
	do_lb_match_c(state);
}

static void lb_match_c_check(void **state)
{
	do_lb_match_c(state);
}

static void lb_nomatch_a_c(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n"
			   "MPATHc WWID1");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	mock_unused_alias("MPATHb");
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, 2);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_a_d_unused(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n"
			   "MPATHd WWID1");
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
			   "MPATHd WWID1");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	mock_used_alias("MPATHb", "WWID2");
	mock_unused_alias("MPATHc");
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, 3);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_a_d_2_used(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n"
			   "MPATHd WWID1");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	mock_used_alias("MPATHb", "WWID2");
	mock_used_alias("MPATHc", "WWID2");
	mock_unused_alias("MPATHe");
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, 5);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_a_d_3_used(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHa WWID0\n"
			   "MPATHd WWID1");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	mock_used_alias("MPATHb", "WWID2");
	mock_used_alias("MPATHc", "WWID2");
	mock_used_alias("MPATHe", "WWID2");
	mock_unused_alias("MPATHf");
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, 6);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_c_a(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHc WWID1\n"
			   "MPATHa WWID0\n");
	mock_unused_alias("MPATHb");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, 2);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_d_a_unused(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHc WWID1\n"
			   "MPATHa WWID0\n"
			   "MPATHd WWID0\n");
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
			   "MPATHd WWID0\n");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	mock_used_alias("MPATHb", "WWID2");
	mock_unused_alias("MPATHe");
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
			   "MPATHb WWID1\n");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	mock_unused_alias("MPATHc");
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, 3);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_a_b_bad(void **state)
{
	int rc;
	char *alias;

	expect_condlog(1, "invalid line 3 in bindings file, missing WWID\n");
	/*
	 * The broken line will be ignored when constructing the bindings vector.
	 * Thus in lookup_binding() MPATHb is never encountered,
	 * and MPATHb appears usable.
	 */
	mock_bindings_file("MPATHa WWID0\n"
			   "MPATHz WWID26\n"
			   "MPATHb\n");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	mock_unused_alias("MPATHb");
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, 2);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_a_b_bad_self(void **state)
{
	int rc;
	char *alias;

	expect_condlog(1, "invalid line 3 in bindings file, missing WWID\n");
	mock_bindings_file("MPATHa WWID0\n"
			   "MPATHz WWID26\n"
			   "MPATHb\n");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	mock_self_alias("MPATHb", "WWID2");
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, 2);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_b_z_a(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHb WWID1\n"
			   "MPATHz WWID26\n"
			   "MPATHa WWID0\n");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	mock_unused_alias("MPATHc");
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, 3);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_b_aa_a(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHb WWID1\n"
			   "MPATHz WWID26\n"
			   "MPATHa WWID0\n");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	mock_unused_alias("MPATHc");
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, 3);
	assert_ptr_equal(alias, NULL);
}

static void fill_bindings(struct strbuf *buf, int start, int end)
{
	int i;

	for (i = start; i <= end; i++) {
		print_strbuf(buf,  "MPATH");
		format_devname(buf, i + 1);
		print_strbuf(buf,  " WWID%d\n", i);
	}
}

static void lb_nomatch_b_a_aa(void **state)
{
	int rc;
	char *alias;
	STRBUF_ON_STACK(buf);

	fill_bindings(&buf, 0, 26);
	mock_bindings_file(get_strbuf_str(&buf));
	expect_condlog(3, NOMATCH_WWID_STR("WWID28"));
	mock_unused_alias("MPATHab");
	rc = lookup_binding(NULL, "WWID28", &alias, "MPATH", 1);
	assert_int_equal(rc, 28);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_b_a_aa_zz(void **state)
{
	int rc;
	char *alias;
	STRBUF_ON_STACK(buf);

	fill_bindings(&buf, 0, 26);
	print_strbuf(&buf, "MPATHzz WWID676\n");
	mock_bindings_file(get_strbuf_str(&buf));
	expect_condlog(3, NOMATCH_WWID_STR("WWID703"));
	mock_unused_alias("MPATHab");
	rc = lookup_binding(NULL, "WWID703", &alias, "MPATH", 1);
	assert_int_equal(rc, 28);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_b_a(void **state)
{
	int rc;
	char *alias;

	mock_bindings_file("MPATHb WWID1\n"
			   "MPATHa WWID0\n");
	expect_condlog(3, NOMATCH_WWID_STR("WWID2"));
	mock_unused_alias("MPATHc");
	rc = lookup_binding(NULL, "WWID2", &alias, "MPATH", 1);
	assert_int_equal(rc, 3);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_b_a_3_used(void **state)
{
	int rc;
	char *alias;
	STRBUF_ON_STACK(buf);

	fill_bindings(&buf, 0, 26);
	mock_bindings_file(get_strbuf_str(&buf));
	expect_condlog(3, NOMATCH_WWID_STR("WWID31"));
	mock_used_alias("MPATHab", "WWID31");
	mock_used_alias("MPATHac", "WWID31");
	mock_used_alias("MPATHad", "WWID31");
	mock_unused_alias("MPATHae");
	rc = lookup_binding(NULL, "WWID31", &alias, "MPATH", 1);
	assert_int_equal(rc, 31);
	assert_ptr_equal(alias, NULL);
}

#ifdef MPATH_ID_INT_MAX
/*
 * The bindings will be sorted by alias. Therefore we have no chance to
 * simulate a "full" table.
 */
static void lb_nomatch_int_max(void **state)
{
	int rc;
	char *alias;
	STRBUF_ON_STACK(buf);

	fill_bindings(&buf, 0, 26);
	print_strbuf(&buf, "MPATH%s WWIDMAX\n", MPATH_ID_INT_MAX);
	mock_bindings_file(get_strbuf_str(&buf));
	expect_condlog(3, NOMATCH_WWID_STR("WWIDNOMORE"));
	mock_unused_alias("MPATHab");
	rc = lookup_binding(NULL, "WWIDNOMORE", &alias, "MPATH", 1);
	assert_int_equal(rc, 28);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_int_max_used(void **state)
{
	int rc;
	char *alias;
	STRBUF_ON_STACK(buf);

	fill_bindings(&buf, 1, 26);
	print_strbuf(&buf, "MPATH%s WWIDMAX\n", MPATH_ID_INT_MAX);
	mock_bindings_file(get_strbuf_str(&buf));
	expect_condlog(3, NOMATCH_WWID_STR("WWIDNOMORE"));
	mock_used_alias("MPATHa", "WWIDNOMORE");
	mock_unused_alias("MPATHab");
	rc = lookup_binding(NULL, "WWIDNOMORE", &alias, "MPATH", 1);
	assert_int_equal(rc, 28);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_int_max_m1(void **state)
{
	int rc;
	char *alias;
	STRBUF_ON_STACK(buf);

	fill_bindings(&buf, 0, 26);
	print_strbuf(&buf, "MPATH%s WWIDMAXM1\n", MPATH_ID_INT_MAX_m1);
	mock_bindings_file(get_strbuf_str(&buf));
	expect_condlog(3, NOMATCH_WWID_STR("WWIDMAX"));
	mock_unused_alias("MPATHab");
	rc = lookup_binding(NULL, "WWIDMAX", &alias, "MPATH", 1);
	assert_int_equal(rc, 28);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_int_max_m1_used(void **state)
{
	int rc;
	char *alias;
	STRBUF_ON_STACK(buf);

	fill_bindings(&buf, 0, 26);
	print_strbuf(&buf, "MPATH%s WWIDMAXM1\n", MPATH_ID_INT_MAX_m1);
	mock_bindings_file(get_strbuf_str(&buf));
	expect_condlog(3, NOMATCH_WWID_STR("WWIDMAX"));
	mock_used_alias("MPATHab", "WWIDMAX");
	mock_unused_alias("MPATHac");
	rc = lookup_binding(NULL, "WWIDMAX", &alias, "MPATH", 1);
	assert_int_equal(rc, 29);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_int_max_m1_1_used(void **state)
{
	int rc;
	char *alias;
	STRBUF_ON_STACK(buf);

	fill_bindings(&buf, 1, 26);
	print_strbuf(&buf, "MPATH%s WWIDMAXM1\n", MPATH_ID_INT_MAX_m1);
	mock_bindings_file(get_strbuf_str(&buf));
	expect_condlog(3, NOMATCH_WWID_STR("WWIDMAX"));
	mock_used_alias("MPATHa", "WWIDMAX");
	mock_unused_alias("MPATHab");
	rc = lookup_binding(NULL, "WWIDMAX", &alias, "MPATH", 1);
	assert_int_equal(rc, 28);
	assert_ptr_equal(alias, NULL);
}

static void lb_nomatch_int_max_m1_2_used(void **state)
{
	int rc;
	char *alias;
	STRBUF_ON_STACK(buf);

	fill_bindings(&buf, 1, 26);
	print_strbuf(&buf, "MPATH%s WWIDMAXM1\n", MPATH_ID_INT_MAX_m1);
	mock_bindings_file(get_strbuf_str(&buf));

	expect_condlog(3, NOMATCH_WWID_STR("WWIDMAX"));
	mock_used_alias("MPATHa", "WWIDMAX");
	mock_used_alias("MPATHab", "WWIDMAX");
	mock_unused_alias("MPATHac");
	rc = lookup_binding(NULL, "WWIDMAX", &alias, "MPATH", 1);
	assert_int_equal(rc, 29);
	assert_ptr_equal(alias, NULL);
}
#endif

static int test_lookup_binding(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_teardown(lb_empty, teardown_bindings),
		cmocka_unit_test_teardown(lb_empty_unused, teardown_bindings),
		cmocka_unit_test_teardown(lb_empty_failed, teardown_bindings),
		cmocka_unit_test_teardown(lb_empty_1_used, teardown_bindings),
		cmocka_unit_test_teardown(lb_empty_1_used_self, teardown_bindings),
		cmocka_unit_test_teardown(lb_match_a, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_a, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_a_bad_check, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_a_unused, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_a_3_used_failed_self, teardown_bindings),
		cmocka_unit_test_teardown(lb_match_c, teardown_bindings),
		cmocka_unit_test_teardown(lb_match_c_check, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_a_c, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_a_d_unused, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_a_d_1_used, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_a_d_2_used, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_a_d_3_used, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_c_a, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_d_a_unused, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_d_a_1_used, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_a_b, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_a_b_bad, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_a_b_bad_self, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_b_z_a, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_b_aa_a, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_b_a_aa, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_b_a_aa_zz, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_b_a, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_b_a_3_used, teardown_bindings),
#ifdef MPATH_ID_INT_MAX
		cmocka_unit_test_teardown(lb_nomatch_int_max, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_int_max_used, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_int_max_m1, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_int_max_m1_used, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_int_max_m1_1_used, teardown_bindings),
		cmocka_unit_test_teardown(lb_nomatch_int_max_m1_2_used, teardown_bindings),
#endif
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

static int rlookup_binding(FILE *dummy, char *buf, const char *alias) {

	const struct binding *bdg;

	bdg = get_binding_for_alias(&global_bindings, alias);
	if (!bdg) {
		return -1;
	}
	strlcpy(buf, bdg->wwid, WWID_SIZE);
	return 0;
}

static void rl_empty(void **state)
{
	int rc;
	char buf[WWID_SIZE];

	buf[0] = '\0';
	mock_bindings_file("");
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
	mock_bindings_file("MPATHa WWID0\n");
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
	mock_bindings_file("MPATHa WWID0\n");
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
	expect_condlog(1, "invalid line 1 in bindings file, missing WWID\n");
	mock_bindings_file("MPATHa     \n");
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
	expect_condlog(3, "Ignoring too large wwid at 1 in bindings file\n");
	mock_bindings_file(line);
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
			   "MPATHb WWID2\n");
	expect_condlog(3, FOUND_ALIAS_STR("MPATHb", "WWID2"));
	rc = rlookup_binding(NULL, buf, "MPATHb");
	assert_int_equal(rc, 0);
	assert_string_equal(buf, "WWID2");
}

static int test_rlookup_binding(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_teardown(rl_empty, teardown_bindings),
		cmocka_unit_test_teardown(rl_match_a, teardown_bindings),
		cmocka_unit_test_teardown(rl_nomatch_a, teardown_bindings),
		cmocka_unit_test_teardown(rl_malformed_a, teardown_bindings),
		cmocka_unit_test_teardown(rl_overlong_a, teardown_bindings),
		cmocka_unit_test_teardown(rl_match_b, teardown_bindings),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

void check_bindings_size(int n)
{
	/* avoid -Waddress problem */
	Bindings *bindings = &global_bindings;

	assert_int_equal(VECTOR_SIZE(bindings), n);
}

static void al_a(void **state)
{
	static const char ln[] = "MPATHa WWIDa\n";
	char *alias;

	expect_value(__wrap_write, count, strlen(BINDINGS_FILE_HEADER) + strlen(ln));
	will_return(__wrap_write, ln);
	will_return(__wrap_write, strlen(BINDINGS_FILE_HEADER) + strlen(ln));
	will_return(__wrap_rename, 0);
	expect_condlog(1, "updated bindings file foo");
	expect_condlog(3, NEW_STR("MPATHa", "WWIDa"));

	alias = allocate_binding("foo", "WWIDa", 1, "MPATH");
	assert_ptr_not_equal(alias, NULL);
	assert_string_equal(alias, "MPATHa");
	check_bindings_size(1);
	free(alias);
}

static void al_zz(void **state)
{
	static const char ln[] = "MPATHzz WWIDzz\n";
	char *alias;

	expect_value(__wrap_write, count, strlen(BINDINGS_FILE_HEADER) + strlen(ln));
	will_return(__wrap_write, ln);
	will_return(__wrap_write, strlen(BINDINGS_FILE_HEADER) + strlen(ln));
	will_return(__wrap_rename, 0);
	expect_condlog(1, "updated bindings file foo");
	expect_condlog(3, NEW_STR("MPATHzz", "WWIDzz"));

	alias = allocate_binding("foo", "WWIDzz", 26*26 + 26, "MPATH");
	assert_ptr_not_equal(alias, NULL);
	assert_string_equal(alias, "MPATHzz");
	check_bindings_size(1);
	free(alias);
}

static void al_0(void **state)
{
	char *alias;

	expect_condlog(0, "allocate_binding: cannot allocate new binding for id 0\n");
	alias = allocate_binding(0, "WWIDa", 0, "MPATH");
	assert_ptr_equal(alias, NULL);
	check_bindings_size(0);
}

static void al_m2(void **state)
{
	char *alias;

	expect_condlog(0, "allocate_binding: cannot allocate new binding for id -2\n");
	alias = allocate_binding(0, "WWIDa", -2, "MPATH");
	assert_ptr_equal(alias, NULL);
	check_bindings_size(0);
}

static void al_write_partial(void **state)
{
	static const char ln[] = "MPATHa WWIDa\n";
	char *alias;

	expect_value(__wrap_write, count, strlen(BINDINGS_FILE_HEADER) + strlen(ln));
	will_return(__wrap_write, ln);
	will_return(__wrap_write, strlen(BINDINGS_FILE_HEADER) + strlen(ln) - 1);
	expect_value(__wrap_write, count, 1);
	will_return(__wrap_write, ln + sizeof(ln) - 2);
	will_return(__wrap_write, 1);
	will_return(__wrap_rename, 0);
	expect_condlog(1, "updated bindings file foo");
	expect_condlog(3, "Created new binding [MPATHa] for WWID [WWIDa]\n");

	alias = allocate_binding("foo", "WWIDa", 1, "MPATH");
	assert_ptr_not_equal(alias, NULL);
	assert_string_equal(alias, "MPATHa");
	check_bindings_size(1);
	free(alias);
}

static void al_write_short(void **state)
{
	static const char ln[] = "MPATHa WWIDa\n";
	char *alias;

	expect_value(__wrap_write, count, strlen(BINDINGS_FILE_HEADER) + strlen(ln));
	will_return(__wrap_write, ln);
	will_return(__wrap_write, strlen(BINDINGS_FILE_HEADER) + strlen(ln) - 1);
	expect_value(__wrap_write, count, 1);
	will_return(__wrap_write, ln + sizeof(ln) - 2);
	will_return(__wrap_write, 0);
	expect_condlog(2, "write_bindings_file: short write");
	expect_condlog(1, "failed to write new bindings file");
	expect_condlog(1, "allocate_binding: deleting binding MPATHa for WWIDa");

	alias = allocate_binding("foo", "WWIDa", 1, "MPATH");
	assert_ptr_equal(alias, NULL);
	check_bindings_size(0);
}

static void al_write_err(void **state)
{
	static const char ln[] = "MPATHa WWIDa\n";
	char *alias;

	expect_value(__wrap_write, count, strlen(BINDINGS_FILE_HEADER) + strlen(ln));
	will_return(__wrap_write, ln);
	will_return(__wrap_write, -EPERM);
	expect_condlog(1, "failed to write new bindings file");
	expect_condlog(1, "allocate_binding: deleting binding MPATHa for WWIDa");

	alias = allocate_binding("foo", "WWIDa", 1, "MPATH");
	assert_ptr_equal(alias, NULL);
	check_bindings_size(0);
}

static void al_rename_err(void **state)
{
	static const char ln[] = "MPATHa WWIDa\n";
	char *alias;

	expect_value(__wrap_write, count, strlen(BINDINGS_FILE_HEADER) + strlen(ln));
	will_return(__wrap_write, ln);
	will_return(__wrap_write, strlen(BINDINGS_FILE_HEADER) + strlen(ln));
	will_return(__wrap_rename, -EROFS);

	expect_condlog(0, "update_bindings_file: rename: Read-only file system");
	expect_condlog(1, "allocate_binding: deleting binding MPATHa for WWIDa");
	alias = allocate_binding("foo", "WWIDa", 1, "MPATH");
	assert_ptr_equal(alias, NULL);
	check_bindings_size(0);
}

static int test_allocate_binding(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_teardown(al_a, teardown_bindings),
		cmocka_unit_test_teardown(al_zz, teardown_bindings),
		cmocka_unit_test_teardown(al_0, teardown_bindings),
		cmocka_unit_test_teardown(al_m2, teardown_bindings),
		cmocka_unit_test_teardown(al_write_partial, teardown_bindings),
		cmocka_unit_test_teardown(al_write_short, teardown_bindings),
		cmocka_unit_test_teardown(al_write_err, teardown_bindings),
		cmocka_unit_test_teardown(al_rename_err, teardown_bindings),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

#define mock_allocate_binding_err_len(alias, wwid, len, err, msg)	\
	do {								\
		static const char ln[] = BINDING_STR(alias, wwid);	\
									\
		expect_value(__wrap_write, count,			\
			     strlen(BINDINGS_FILE_HEADER) + (len) + strlen(ln)); \
		will_return(__wrap_write, ln);				\
		will_return(__wrap_write,				\
			    strlen(BINDINGS_FILE_HEADER) + (len) + strlen(ln)); \
		will_return(__wrap_rename, err);			\
		if (err == 0) {						\
			expect_condlog(1, "updated bindings file x\n");	\
			expect_condlog(3, NEW_STR(alias, wwid));	\
		} else {						\
			expect_condlog(0, "update_bindings_file: rename: " msg "\n"); \
			expect_condlog(1, "allocate_binding: deleting binding "	\
				       alias " for " wwid "\n");	\
		}							\
	} while (0)

#define mock_allocate_binding_err(alias, wwid, err, msg)	\
	mock_allocate_binding_err_len(alias, wwid, 0, err, msg)

#define mock_allocate_binding(alias, wwid)			\
	mock_allocate_binding_err(alias, wwid, 0, "")

#define mock_allocate_binding_len(alias, wwid, len)			\
	mock_allocate_binding_err_len(alias, wwid, len, 0, "")

static void gufa_empty_new_rw(void **state) {
	char *alias;

	mock_bindings_file("");
	mock_unused_alias("MPATHa");
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));

	mock_allocate_binding("MPATHa", "WWID0");
	alias = get_user_friendly_alias("WWID0", "x", "", "MPATH", false);
	assert_string_equal(alias, "MPATHa");
	free(alias);
}

static void gufa_empty_new_ro_1(void **state) {
	char *alias;

	mock_bindings_file("");
	mock_unused_alias("MPATHa");
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));
	mock_allocate_binding_err("MPATHa", "WWID0", -EROFS, "Read-only file system");

	alias = get_user_friendly_alias("WWID0", "x", "", "MPATH", false);
	assert_ptr_equal(alias, NULL);
}

static void gufa_empty_new_ro_2(void **state) {
	char *alias;

	mock_bindings_file("");
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));
	mock_unused_alias("MPATHa");

	alias = get_user_friendly_alias("WWID0", "x", "", "MPATH", true);
	assert_ptr_equal(alias, NULL);
}

static void gufa_match_a_unused(void **state) {
	char *alias;

	mock_bindings_file("MPATHa WWID0");
	expect_condlog(3, FOUND_STR("MPATHa", "WWID0"));
	mock_unused_alias("MPATHa");
	expect_condlog(3, EXISTING_STR("MPATHa", "WWID0"));

	alias = get_user_friendly_alias("WWID0", "x", "", "MPATH", true);
	assert_string_equal(alias, "MPATHa");
	free(alias);
}

static void gufa_match_a_self(void **state) {
	char *alias;

	mock_bindings_file("MPATHa WWID0");
	expect_condlog(3, FOUND_STR("MPATHa", "WWID0"));
	mock_self_alias("MPATHa", "WWID0");
	expect_condlog(3, EXISTING_STR("MPATHa", "WWID0"));

	alias = get_user_friendly_alias("WWID0", "x", "", "MPATH", true);
	assert_string_equal(alias, "MPATHa");
	free(alias);
}

static void gufa_match_a_used(void **state) {
	char *alias;


	mock_bindings_file("MPATHa WWID0");
	expect_condlog(3, FOUND_STR("MPATHa", "WWID0"));
	mock_used_alias("MPATHa", "WWID0");

	alias = get_user_friendly_alias("WWID0", "x", "", "MPATH", true);
	assert_ptr_equal(alias, NULL);
}

static void gufa_nomatch_a_c(void **state) {
	char *alias;
	static const char bindings[] = ("MPATHa WWID0\n"
					"MPATHc WWID2\n");

	mock_bindings_file(bindings);
	mock_unused_alias("MPATHb");
	expect_condlog(3, NOMATCH_WWID_STR("WWID1"));

	mock_allocate_binding_len("MPATHb", "WWID1", strlen(bindings));

	alias = get_user_friendly_alias("WWID1", "x", "", "MPATH", false);
	assert_string_equal(alias, "MPATHb");
	free(alias);
}

static void gufa_nomatch_c_a(void **state) {
	char *alias;
	const char bindings[] = ("MPATHc WWID2\n"
				 "MPATHa WWID0\n");

	mock_bindings_file(bindings);
	mock_unused_alias("MPATHb");
	expect_condlog(3, NOMATCH_WWID_STR("WWID1"));

	mock_allocate_binding_len("MPATHb", "WWID1", sizeof(bindings) - 1);

	alias = get_user_friendly_alias("WWID1", "x", "", "MPATH", false);
	assert_string_equal(alias, "MPATHb");
	free(alias);
}

static void gufa_nomatch_c_b(void **state) {
	char *alias;
	const char bindings[] = ("MPATHc WWID2\n"
				 "MPATHb WWID1\n");

	mock_bindings_file(bindings);
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));
	mock_unused_alias("MPATHa");

	mock_allocate_binding_len("MPATHa", "WWID0", sizeof(bindings) - 1);

	alias = get_user_friendly_alias("WWID0", "x", "", "MPATH", false);
	assert_string_equal(alias, "MPATHa");
	free(alias);
}

static void gufa_nomatch_c_b_used(void **state) {
	char *alias;
	const char bindings[] = ("MPATHc WWID2\n"
				 "MPATHb WWID1\n");

	mock_bindings_file(bindings);
	expect_condlog(3, NOMATCH_WWID_STR("WWID4"));
	mock_used_alias("MPATHa", "WWID4");
	mock_unused_alias("MPATHd");

	mock_allocate_binding_len("MPATHd", "WWID4", sizeof(bindings) - 1);

	alias = get_user_friendly_alias("WWID4", "x", "", "MPATH", false);
	assert_string_equal(alias, "MPATHd");
	free(alias);
}

static void gufa_nomatch_b_f_a(void **state) {
	char *alias;
	const char bindings[] = ("MPATHb WWID1\n"
				 "MPATHf WWID6\n"
				 "MPATHa WWID0\n");

	mock_bindings_file(bindings);
	expect_condlog(3, NOMATCH_WWID_STR("WWID7"));
	mock_unused_alias("MPATHc");

	mock_allocate_binding_len("MPATHc", "WWID7", sizeof(bindings) - 1);

	alias = get_user_friendly_alias("WWID7", "x", "", "MPATH", false);
	assert_string_equal(alias, "MPATHc");
	free(alias);
}

static void gufa_nomatch_b_aa_a(void **state) {
	char *alias;
	STRBUF_ON_STACK(buf);

	fill_bindings(&buf, 0, 26);
	mock_bindings_file(get_strbuf_str(&buf));
	expect_condlog(3, NOMATCH_WWID_STR("WWID28"));
	mock_unused_alias("MPATHab");
	mock_allocate_binding_len("MPATHab", "WWID28", get_strbuf_len(&buf));

	alias = get_user_friendly_alias("WWID28", "x", "", "MPATH", false);
	assert_string_equal(alias, "MPATHab");
	free(alias);
}

static void gufa_nomatch_b_f_a_sorted(void **state) {
	char *alias;
	const char bindings[] = ("MPATHb WWID1\n"
				 "MPATHf WWID6\n"
				 "MPATHa WWID0\n");

	mock_bindings_file(bindings);
	expect_condlog(3, NOMATCH_WWID_STR("WWID7"));
	mock_unused_alias("MPATHc");

	mock_allocate_binding_len("MPATHc", "WWID7", sizeof(bindings) - 1);

	alias = get_user_friendly_alias("WWID7", "x", "", "MPATH", false);
	assert_string_equal(alias, "MPATHc");
	free(alias);
}

static void gufa_old_empty(void **state) {
	char *alias;

	/* rlookup_binding for ALIAS */
	mock_bindings_file("");
	expect_condlog(3, NOMATCH_STR("MPATHz"));
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));

	mock_allocate_binding("MPATHz", "WWID0");
	expect_condlog(2, ALLOC_STR("MPATHz", "WWID0"));

	alias = get_user_friendly_alias("WWID0", "x", "MPATHz", "MPATH", false);
	assert_string_equal(alias, "MPATHz");
	free(alias);
}

static void gufa_old_match(void **state) {
	char *alias;

	mock_bindings_file("MPATHb WWID1\n"
			   "MPATHz WWID0");
	expect_condlog(3, FOUND_ALIAS_STR("MPATHz", "WWID0"));

	alias = get_user_friendly_alias("WWID0", "x", "MPATHz", "MPATH", false);
	assert_string_equal(alias, "MPATHz");
	free(alias);
}

static void gufa_old_match_other(void **state) {
	char *alias;
	static const char bindings[] = "MPATHz WWID9\n";

	mock_bindings_file(bindings);
	expect_condlog(3, FOUND_ALIAS_STR("MPATHz", "WWID9"));
	expect_condlog(0, REUSE_STR("MPATHz", "WWID9"));
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));
	mock_unused_alias("MPATHa");

	mock_allocate_binding_len("MPATHa", "WWID0", sizeof(bindings) - 1);

	alias = get_user_friendly_alias("WWID0", "x", "MPATHz", "MPATH", false);
	assert_string_equal(alias, "MPATHa");
	free(alias);
}

static void gufa_old_match_other_used(void **state) {
	char *alias;
	static const char bindings[] = "MPATHz WWID9\n";

	mock_bindings_file(bindings);
	expect_condlog(3, FOUND_ALIAS_STR("MPATHz", "WWID9"));
	expect_condlog(0, REUSE_STR("MPATHz", "WWID9"));
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));
	mock_used_alias("MPATHa", "WWID0");
	mock_unused_alias("MPATHb");

	mock_allocate_binding_len("MPATHb", "WWID0", sizeof(bindings) - 1);
	alias = get_user_friendly_alias("WWID0", "x", "MPATHz", "MPATH", false);
	assert_string_equal(alias, "MPATHb");
	free(alias);
}

static void gufa_old_match_other_wwidmatch(void **state) {
	char *alias;
	static const char bindings[] = ("MPATHz WWID9\n"
					"MPATHc WWID2");

	mock_bindings_file(bindings);
	expect_condlog(3, FOUND_ALIAS_STR("MPATHz", "WWID9"));
	expect_condlog(0, REUSE_STR("MPATHz", "WWID9"));
	expect_condlog(3, FOUND_STR("MPATHc", "WWID2"));
	mock_unused_alias("MPATHc");
	expect_condlog(3, EXISTING_STR("MPATHc", "WWID2"));

	alias = get_user_friendly_alias("WWID2", "x", "MPATHz", "MPATH", false);
	assert_string_equal(alias, "MPATHc");
	free(alias);
}

static void gufa_old_match_other_wwidmatch_used(void **state) {
	char *alias;
	static const char bindings[] = ("MPATHz WWID9\n"
					"MPATHc WWID2");

	mock_bindings_file(bindings);
	expect_condlog(3, FOUND_ALIAS_STR("MPATHz", "WWID9"));
	expect_condlog(0, REUSE_STR("MPATHz", "WWID9"));
	expect_condlog(3, FOUND_STR("MPATHc", "WWID2"));
	mock_used_alias("MPATHc", "WWID2");

	alias = get_user_friendly_alias("WWID2", "x", "MPATHz", "MPATH", false);
	assert_ptr_equal(alias, NULL);
}

static void gufa_old_nomatch_wwidmatch(void **state) {
	char *alias;
	static const char bindings[] = "MPATHa WWID0";

	mock_bindings_file(bindings);
	expect_condlog(3, NOMATCH_STR("MPATHz"));
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

	mock_bindings_file(bindings);
	expect_condlog(3, NOMATCH_STR("MPATHz"));
	expect_condlog(3, FOUND_STR("MPATHa", "WWID0"));
	mock_used_alias("MPATHa", "WWID0");

	alias = get_user_friendly_alias("WWID0", "x", "MPATHz", "MPATH", false);
	assert_ptr_equal(alias, NULL);
}

static void gufa_old_nomatch_nowwidmatch(void **state) {
	char *alias;
	static const char bindings[] = "MPATHb WWID1\n";

	mock_bindings_file(bindings);
	expect_condlog(3, NOMATCH_STR("MPATHz"));
	expect_condlog(3, NOMATCH_WWID_STR("WWID0"));

	mock_allocate_binding_len("MPATHz", "WWID0", sizeof(bindings) - 1);
	expect_condlog(2, ALLOC_STR("MPATHz", "WWID0"));

	alias = get_user_friendly_alias("WWID0", "x", "MPATHz", "MPATH", false);
	assert_string_equal(alias, "MPATHz");
	free(alias);
}

static int test_get_user_friendly_alias()
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_teardown(gufa_empty_new_rw, teardown_bindings),
		cmocka_unit_test_teardown(gufa_empty_new_ro_1, teardown_bindings),
		cmocka_unit_test_teardown(gufa_empty_new_ro_2, teardown_bindings),
		cmocka_unit_test_teardown(gufa_match_a_unused, teardown_bindings),
		cmocka_unit_test_teardown(gufa_match_a_self, teardown_bindings),
		cmocka_unit_test_teardown(gufa_match_a_used, teardown_bindings),
		cmocka_unit_test_teardown(gufa_nomatch_a_c, teardown_bindings),
		cmocka_unit_test_teardown(gufa_nomatch_c_a, teardown_bindings),
		cmocka_unit_test_teardown(gufa_nomatch_c_b, teardown_bindings),
		cmocka_unit_test_teardown(gufa_nomatch_c_b_used, teardown_bindings),
		cmocka_unit_test_teardown(gufa_nomatch_b_f_a, teardown_bindings),
		cmocka_unit_test_teardown(gufa_nomatch_b_aa_a, teardown_bindings),
		cmocka_unit_test_teardown(gufa_nomatch_b_f_a_sorted, teardown_bindings),
		cmocka_unit_test_teardown(gufa_old_empty, teardown_bindings),
		cmocka_unit_test_teardown(gufa_old_match, teardown_bindings),
		cmocka_unit_test_teardown(gufa_old_match_other, teardown_bindings),
		cmocka_unit_test_teardown(gufa_old_match_other_used, teardown_bindings),
		cmocka_unit_test_teardown(gufa_old_match_other_wwidmatch, teardown_bindings),
		cmocka_unit_test_teardown(gufa_old_match_other_wwidmatch_used, teardown_bindings),
		cmocka_unit_test_teardown(gufa_old_nomatch_wwidmatch, teardown_bindings),
		cmocka_unit_test_teardown(gufa_old_nomatch_wwidmatch_used, teardown_bindings),
		cmocka_unit_test_teardown(gufa_old_nomatch_nowwidmatch, teardown_bindings),
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
	ret += test_get_user_friendly_alias();

	return ret;
}
