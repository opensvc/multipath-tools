/* Set BROKEN to 1 to treat broken behavior as success */
#define BROKEN 1
#define VERBOSITY 2

#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <cmocka.h>
#include <libudev.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <sys/sysmacros.h>
#include "structs.h"
#include "structs_vec.h"
#include "config.h"
#include "debug.h"
#include "defaults.h"
#include "pgpolicies.h"
#include "test-lib.h"
#include "print.h"
#include "util.h"

#define N_CONF_FILES 2

static const char tmplate[] = "/tmp/hwtable-XXXXXX";
/* pretend new dm, use minio_rq */
static const unsigned int dm_tgt_version[3] = { 1, 1, 1 };

struct key_value {
	const char *key;
	const char *value;
};

struct hwt_state {
	char *tmpname;
	char *dirname;
	FILE *config_file;
	FILE *conf_dir_file[N_CONF_FILES];
	struct vectors *vecs;
	void (*test)(const struct hwt_state *);
	const char *test_name;
};

#define SET_TEST_FUNC(hwt, func) do {		\
		hwt->test = func;		\
		hwt->test_name = #func;		\
	} while (0)

static struct config *_conf;
struct udev *udev;
int logsink = -1;

struct config *get_multipath_config(void)
{
	return _conf;
}

void put_multipath_config(void *arg)
{}

void make_config_file_path(char *buf, int buflen,
			  const struct hwt_state *hwt, int i)
{
	static const char fn_template[] = "%s/test-%02d.conf";

	if (i == -1)
		/* main config file */
		snprintf(buf, buflen, fn_template, hwt->tmpname, 0);
	else
		snprintf(buf, buflen, fn_template, hwt->dirname, i);
}

static void reset_vecs(struct vectors *vecs)
{
	remove_maps(vecs);
	free_pathvec(vecs->pathvec, FREE_PATHS);

	vecs->pathvec = vector_alloc();
	assert_ptr_not_equal(vecs->pathvec, NULL);
	vecs->mpvec = vector_alloc();
	assert_ptr_not_equal(vecs->mpvec, NULL);
}

static void free_hwt(struct hwt_state *hwt)
{
	char buf[PATH_MAX];
	int i;

	if (hwt->config_file != NULL)
		fclose(hwt->config_file);
	for (i = 0; i < N_CONF_FILES; i++) {
		if (hwt->conf_dir_file[i] != NULL)
			fclose(hwt->conf_dir_file[i]);
	}

	if (hwt->tmpname != NULL) {
		make_config_file_path(buf, sizeof(buf), hwt, -1);
		unlink(buf);
		rmdir(hwt->tmpname);
		free(hwt->tmpname);
	}

	if (hwt->dirname != NULL) {
		for (i = 0; i < N_CONF_FILES; i++) {
			make_config_file_path(buf, sizeof(buf), hwt, i);
			unlink(buf);
		}
		rmdir(hwt->dirname);
		free(hwt->dirname);
	}

	if (hwt->vecs != NULL) {
		if (hwt->vecs->mpvec != NULL)
			remove_maps(hwt->vecs);
		if (hwt->vecs->pathvec != NULL)
			free_pathvec(hwt->vecs->pathvec, FREE_PATHS);
		pthread_mutex_destroy(&hwt->vecs->lock.mutex);
		free(hwt->vecs);
	}
	free(hwt);
}

static int setup(void **state)
{
	struct hwt_state *hwt;
	char buf[PATH_MAX];
	int i;

	*state = NULL;
	hwt = calloc(1, sizeof(*hwt));
	if (hwt == NULL)
		return -1;

	snprintf(buf, sizeof(buf), "%s", tmplate);
	if (mkdtemp(buf) == NULL) {
		condlog(0, "mkdtemp: %s", strerror(errno));
		goto err;
	}
	hwt->tmpname = strdup(buf);

	snprintf(buf, sizeof(buf), "%s", tmplate);
	if (mkdtemp(buf) == NULL) {
		condlog(0, "mkdtemp (2): %s", strerror(errno));
		goto err;
	}
	hwt->dirname = strdup(buf);

	make_config_file_path(buf, sizeof(buf), hwt, -1);
	hwt->config_file = fopen(buf, "w+");
	if (hwt->config_file == NULL)
		goto err;

	for (i = 0; i < N_CONF_FILES; i++) {
		make_config_file_path(buf, sizeof(buf), hwt, i);
		hwt->conf_dir_file[i] = fopen(buf, "w+");
		if (hwt->conf_dir_file[i] == NULL)
			goto err;
	}

	hwt->vecs = calloc(1, sizeof(*hwt->vecs));
	if (hwt->vecs == NULL)
		goto err;
	pthread_mutex_init(&hwt->vecs->lock.mutex, NULL);
	hwt->vecs->pathvec = vector_alloc();
	hwt->vecs->mpvec = vector_alloc();
	if (hwt->vecs->pathvec == NULL || hwt->vecs->mpvec == NULL)
		goto err;

	*state = hwt;
	return 0;

err:
	free_hwt(hwt);
	return -1;
}

static int teardown(void **state)
{
	if (state == NULL || *state == NULL)
		return -1;

	free_hwt(*state);
	*state = NULL;

	return 0;
}

/*
 * Helpers for creating the config file(s)
 */

static void reset_config(FILE *ff)
{
	if (ff == NULL)
		return;
	rewind(ff);
	if (ftruncate(fileno(ff), 0) == -1)
		condlog(1, "ftruncate: %s", strerror(errno));
}

static void reset_configs(const struct hwt_state *hwt)
{
	int i;

	reset_config(hwt->config_file);
	for (i = 0; i < N_CONF_FILES; i++)
		reset_config(hwt->conf_dir_file[i]);
}

static void write_key_values(FILE *ff, int nkv, const struct key_value *kv)
{
	int i;

	for (i = 0; i < nkv; i++) {
		if (strchr(kv[i].value, ' ') == NULL &&
		    strchr(kv[i].value, '\"') == NULL)
			fprintf(ff, "\t%s %s\n", kv[i].key, kv[i].value);
		else
			fprintf(ff, "\t%s \"%s\"\n", kv[i].key, kv[i].value);
	}
}

static void begin_section(FILE *ff, const char *section)
{
	fprintf(ff, "%s {\n", section);
}

static void end_section(FILE *ff)
{
	fprintf(ff, "}\n");
}

static void write_section(FILE *ff, const char *section,
			  int nkv, const struct key_value *kv)
{
	begin_section(ff, section);
	write_key_values(ff, nkv, kv);
	end_section(ff);
}

static void write_defaults(const struct hwt_state *hwt)
{
	static const char bindings_name[] = "bindings";
	static struct key_value defaults[] = {
		{ "config_dir", NULL },
		{ "bindings_file", NULL },
		{ "multipath_dir", NULL },
		{ "detect_prio", "no" },
		{ "detect_checker", "no" },
	};
	char buf[sizeof(tmplate) + sizeof(bindings_name)];
	char dirbuf[PATH_MAX];

	snprintf(buf, sizeof(buf), "%s/%s", hwt->tmpname, bindings_name);
	defaults[0].value = hwt->dirname;
	defaults[1].value = buf;
	assert_ptr_not_equal(getcwd(dirbuf, sizeof(dirbuf)), NULL);
	strncat(dirbuf, "/lib", sizeof(dirbuf));
	defaults[2].value = dirbuf;
	write_section(hwt->config_file, "defaults",
		      ARRAY_SIZE(defaults), defaults);
}

static void begin_config(const struct hwt_state *hwt)
{
	reset_configs(hwt);
	write_defaults(hwt);
}

static void begin_section_all(const struct hwt_state *hwt, const char *section)
{
	int i;

	begin_section(hwt->config_file, section);
	for (i = 0; i < N_CONF_FILES; i++)
		begin_section(hwt->conf_dir_file[i], section);
}

static void end_section_all(const struct hwt_state *hwt)
{
	int i;

	end_section(hwt->config_file);
	for (i = 0; i < N_CONF_FILES; i++)
		end_section(hwt->conf_dir_file[i]);
}

static void finish_config(const struct hwt_state *hwt)
{
	int i;

	fflush(hwt->config_file);
	for (i = 0; i < N_CONF_FILES; i++) {
		fflush(hwt->conf_dir_file[i]);
	}
}

static void write_device(FILE *ff, int nkv, const struct key_value *kv)
{
	write_section(ff, "device", nkv, kv);
}

/*
 * Some macros to avoid boilerplace code
 */

#define CHECK_STATE(state) ({ \
	assert_ptr_not_equal(state, NULL); \
	assert_ptr_not_equal(*(state), NULL);	\
	*state; })

#define WRITE_EMPTY_CONF(hwt) do {				\
		begin_config(hwt);				\
		finish_config(hwt);				\
	} while (0)

#define WRITE_ONE_DEVICE(hwt, kv) do {					\
		begin_config(hwt);					\
		begin_section_all(hwt, "devices");			\
		write_device(hwt->config_file, ARRAY_SIZE(kv), kv);	\
		end_section_all(hwt);					\
		finish_config(hwt);					\
	} while (0)

#define WRITE_TWO_DEVICES(hwt, kv1, kv2) do {				\
		begin_config(hwt);					\
		begin_section_all(hwt, "devices");			\
		write_device(hwt->config_file, ARRAY_SIZE(kv1), kv1);	\
		write_device(hwt->config_file, ARRAY_SIZE(kv2), kv2);	\
		end_section_all(hwt);					\
		finish_config(hwt);					\
	} while (0)

#define WRITE_TWO_DEVICES_W_DIR(hwt, kv1, kv2) do {			\
		begin_config(hwt);					\
		begin_section_all(hwt, "devices");			\
		write_device(hwt->config_file, ARRAY_SIZE(kv1), kv1);	\
		write_device(hwt->conf_dir_file[0],			\
			     ARRAY_SIZE(kv2), kv2);			\
		end_section_all(hwt);					\
		finish_config(hwt);					\
	} while (0)

#define LOAD_CONFIG(hwt) ({ \
	char buf[PATH_MAX];	   \
	struct config *__cf;						\
									\
	make_config_file_path(buf, sizeof(buf), hwt, -1);		\
	__cf = load_config(buf);					\
	assert_ptr_not_equal(__cf, NULL);				\
	assert_ptr_not_equal(__cf->hwtable, NULL);			\
	__cf->verbosity = VERBOSITY;					\
	memcpy(&__cf->version, dm_tgt_version, sizeof(__cf->version));	\
	__cf; })

#define FREE_CONFIG(conf) do {			\
		free_config(conf);		\
		conf = NULL;			\
	} while (0)

static void replace_config(const struct hwt_state *hwt,
			   const char *conf_str)
{
	FREE_CONFIG(_conf);
	reset_configs(hwt);
	fprintf(hwt->config_file, "%s", conf_str);
	fflush(hwt->config_file);
	_conf = LOAD_CONFIG(hwt);
}

#define TEST_PROP(prop, val) do {				\
		if (val == NULL)				\
			assert_ptr_equal(prop, NULL);		\
		else {						\
			assert_ptr_not_equal(prop, NULL);	\
			assert_string_equal(prop, val);		\
		}						\
	} while (0)

#if BROKEN
#define TEST_PROP_BROKEN(name, prop, bad, good) do {			\
		condlog(1, "%s: WARNING: Broken test for %s == \"%s\" on line %d, should be \"%s\"", \
			__func__, name, bad ? bad : "NULL",		\
			__LINE__, good ? good : "NULL");			\
		TEST_PROP(prop, bad);					\
	} while (0)
#else
#define TEST_PROP_BROKEN(name, prop, bad, good) TEST_PROP(prop, good)
#endif

/*
 * Some predefined key/value pairs
 */

static const char _wwid[] = "wwid";
static const char _vendor[] = "vendor";
static const char _product[] = "product";
static const char _prio[] = "prio";
static const char _checker[] = "path_checker";
static const char _getuid[] = "getuid_callout";
static const char _uid_attr[] = "uid_attribute";
static const char _bl_product[] = "product_blacklist";
static const char _minio[] = "rr_min_io_rq";
static const char _no_path_retry[] = "no_path_retry";

/* Device identifiers */
static const struct key_value vnd_foo = { _vendor, "foo" };
static const struct key_value prd_bar = { _product, "bar" };
static const struct key_value prd_bam = { _product, "bam" };
static const struct key_value prd_baq = { _product, "\"bar\"" };
static const struct key_value prd_baqq = { _product, "\"\"bar\"\"" };
static const struct key_value prd_barz = { _product, "barz" };
static const struct key_value vnd_boo = { _vendor, "boo" };
static const struct key_value prd_baz = { _product, "baz" };
static const struct key_value wwid_test = { _wwid, default_wwid };

/* Regular expresssions */
static const struct key_value vnd__oo = { _vendor, ".oo" };
static const struct key_value vnd_t_oo = { _vendor, "^.oo" };
static const struct key_value prd_ba_ = { _product, "ba." };
static const struct key_value prd_ba_s = { _product, "(bar|baz|ba\\.)$" };
/* Pathological cases, see below */
static const struct key_value prd_barx = { _product, "ba[[rxy]" };
static const struct key_value prd_bazy = { _product, "ba[zy]" };
static const struct key_value prd_bazy1 = { _product, "ba(z|y)" };

/* Properties */
static const struct key_value prio_emc = { _prio, "emc" };
static const struct key_value prio_hds = { _prio, "hds" };
static const struct key_value prio_rdac = { _prio, "rdac" };
static const struct key_value chk_hp = { _checker, "hp_sw" };
static const struct key_value gui_foo = { _getuid, "/tmp/foo" };
static const struct key_value uid_baz = { _uid_attr, "BAZ_ATTR" };
static const struct key_value bl_bar = { _bl_product, "bar" };
static const struct key_value bl_baz = { _bl_product, "baz" };
static const struct key_value bl_barx = { _bl_product, "ba[[rxy]" };
static const struct key_value bl_bazy = { _bl_product, "ba[zy]" };
static const struct key_value minio_99 = { _minio, "99" };
static const struct key_value npr_37 = { _no_path_retry, "37" };
static const struct key_value npr_queue = { _no_path_retry, "queue" };

/***** BEGIN TESTS SECTION *****/

/*
 * Dump the configuration, subistitute the dumped configuration
 * for the current one, and verify that the result is identical.
 */
static void replicate_config(const struct hwt_state *hwt, bool local)
{
	char *cfg1, *cfg2;
	vector hwtable;
	struct config *conf;

	condlog(3, "--- %s: replicating %s configuration", __func__,
		local ? "local" : "full");

	conf = get_multipath_config();
	if (!local)
		/* "full" configuration */
		cfg1 = snprint_config(conf, NULL, NULL, NULL);
	else {
		/* "local" configuration */
		hwtable = get_used_hwes(hwt->vecs->pathvec);
		cfg1 = snprint_config(conf, NULL, hwtable, hwt->vecs->mpvec);
	}

	assert_non_null(cfg1);
	put_multipath_config(conf);

	replace_config(hwt, cfg1);

	/*
	 * The local configuration adds multipath entries, and may move device
	 * entries for local devices to the end of the list. Identical config
	 * strings therefore can't be expected in the "local" case.
	 * That doesn't matter. The important thing is that, with the reloaded
	 * configuration, the test case still passes.
	 */
	if (local) {
		free(cfg1);
		return;
	}

	conf = get_multipath_config();
	cfg2 = snprint_config(conf, NULL, NULL, NULL);
	assert_non_null(cfg2);
	put_multipath_config(conf);

// #define DBG_CONFIG 1
#ifdef DBG_CONFIG
#define DUMP_CFG_STR(x) do {						\
		FILE *tmp = fopen("/tmp/hwtable-" #x ".txt", "w");	\
		fprintf(tmp, "%s", x);					\
		fclose(tmp);						\
	} while (0)

	DUMP_CFG_STR(cfg1);
	DUMP_CFG_STR(cfg2);
#endif

	assert_int_equal(strlen(cfg2), strlen(cfg1));
	assert_string_equal(cfg2, cfg1);
	free(cfg1);
	free(cfg2);
}

/*
 * Run hwt->test three times; once with the constructed configuration,
 * once after re-reading the full dumped configuration, and once with the
 * dumped local configuration.
 *
 * Expected: test passes every time.
 */
static void test_driver(void **state)
{
	const struct hwt_state *hwt;

	hwt = CHECK_STATE(state);
	_conf = LOAD_CONFIG(hwt);
	hwt->test(hwt);

	replicate_config(hwt, false);
	reset_vecs(hwt->vecs);
	hwt->test(hwt);

	replicate_config(hwt, true);
	reset_vecs(hwt->vecs);
	hwt->test(hwt);

	reset_vecs(hwt->vecs);
	FREE_CONFIG(_conf);
}

/*
 * Sanity check for the test itself, because defaults may be changed
 * in libmultipath.
 *
 * Our checking for match or non-match relies on the defaults being
 * different from what our device sections contain.
 */
static void test_sanity_globals(void **state)
{
	assert_string_not_equal(prio_emc.value, DEFAULT_PRIO);
	assert_string_not_equal(prio_hds.value, DEFAULT_PRIO);
	assert_string_not_equal(chk_hp.value, DEFAULT_CHECKER);
	assert_int_not_equal(MULTIBUS, DEFAULT_PGPOLICY);
	assert_int_not_equal(NO_PATH_RETRY_QUEUE, DEFAULT_NO_PATH_RETRY);
	assert_int_not_equal(atoi(minio_99.value), DEFAULT_MINIO_RQ);
	assert_int_not_equal(atoi(npr_37.value), DEFAULT_NO_PATH_RETRY);
}

/*
 * Regression test for internal hwtable. NVME is an example of two entries
 * in the built-in hwtable, one if which matches a subset of the other.
 */
static void test_internal_nvme(const struct hwt_state *hwt)
{
	struct path *pp;
	struct multipath *mp;

	/*
	 * Generic NVMe: expect defaults for pgpolicy and no_path_retry
	 */
	pp = mock_path("NVME", "NoName");
	mp = mock_multipath(pp);
	assert_ptr_not_equal(mp, NULL);
	TEST_PROP(checker_name(&pp->checker), NONE);
	TEST_PROP(pp->uid_attribute, DEFAULT_NVME_UID_ATTRIBUTE);
	assert_int_equal(mp->pgpolicy, DEFAULT_PGPOLICY);
	assert_int_equal(mp->no_path_retry, DEFAULT_NO_PATH_RETRY);
	assert_int_equal(mp->retain_hwhandler, RETAIN_HWHANDLER_OFF);

	/*
	 * NetApp NVMe: expect special values for pgpolicy and no_path_retry
	 */
	pp = mock_path_wwid("NVME", "NetApp ONTAP Controller",
			    default_wwid_1);
	mp = mock_multipath(pp);
	assert_ptr_not_equal(mp, NULL);
	TEST_PROP(checker_name(&pp->checker), NONE);
	TEST_PROP(pp->uid_attribute, "ID_WWN");
	assert_int_equal(mp->pgpolicy, MULTIBUS);
	assert_int_equal(mp->no_path_retry, NO_PATH_RETRY_QUEUE);
	assert_int_equal(mp->retain_hwhandler, RETAIN_HWHANDLER_OFF);
}

static int setup_internal_nvme(void **state)
{
	struct hwt_state *hwt = CHECK_STATE(state);

	WRITE_EMPTY_CONF(hwt);
	SET_TEST_FUNC(hwt, test_internal_nvme);

	return 0;
}

/*
 * Device section with a simple entry qith double quotes ('foo:"bar"')
 */
static void test_quoted_hwe(const struct hwt_state *hwt)
{
	struct path *pp;

	/* foo:"bar" matches */
	pp = mock_path(vnd_foo.value, prd_baq.value);
	TEST_PROP(prio_name(&pp->prio), prio_emc.value);

	/* foo:bar doesn't match */
	pp = mock_path(vnd_foo.value, prd_bar.value);
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);
}

static int setup_quoted_hwe(void **state)
{
	struct hwt_state *hwt = CHECK_STATE(state);
	const struct key_value kv[] = { vnd_foo, prd_baqq, prio_emc };

	WRITE_ONE_DEVICE(hwt, kv);
	SET_TEST_FUNC(hwt, test_quoted_hwe);
	return 0;
}

/*
 * Device section with a single simple entry ("foo:bar")
 */
static void test_string_hwe(const struct hwt_state *hwt)
{
	struct path *pp;

	/* foo:bar matches */
	pp = mock_path(vnd_foo.value, prd_bar.value);
	TEST_PROP(prio_name(&pp->prio), prio_emc.value);

	/* foo:baz doesn't match */
	pp = mock_path(vnd_foo.value, prd_baz.value);
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);

	/* boo:bar doesn't match */
	pp = mock_path(vnd_boo.value, prd_bar.value);
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);
}

static int setup_string_hwe(void **state)
{
	struct hwt_state *hwt = CHECK_STATE(state);
	const struct key_value kv[] = { vnd_foo, prd_bar, prio_emc };

	WRITE_ONE_DEVICE(hwt, kv);
	SET_TEST_FUNC(hwt, test_string_hwe);
	return 0;
}

/*
 * Device section with a broken entry (no product)
 * It should be ignored.
 */
static void test_broken_hwe(const struct hwt_state *hwt)
{
	struct path *pp;

	/* foo:bar doesn't match, as hwentry is ignored */
	pp = mock_path(vnd_foo.value, prd_bar.value);
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);

	/* boo:bar doesn't match */
	pp = mock_path(vnd_boo.value, prd_bar.value);
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);
}

static int setup_broken_hwe(void **state)
{
	struct hwt_state *hwt = CHECK_STATE(state);
	const struct key_value kv[] = { vnd_foo, prio_emc };

	WRITE_ONE_DEVICE(hwt, kv);
	SET_TEST_FUNC(hwt, test_broken_hwe);
	return 0;
}

/*
 * Like test_broken_hwe, but in config_dir file.
 */
static int setup_broken_hwe_dir(void **state)
{
	struct hwt_state *hwt = CHECK_STATE(state);
	const struct key_value kv[] = { vnd_foo, prio_emc };

	begin_config(hwt);
	begin_section_all(hwt, "devices");
	write_device(hwt->conf_dir_file[0], ARRAY_SIZE(kv), kv);
	end_section_all(hwt);
	finish_config(hwt);
	hwt->test = test_broken_hwe;
	hwt->test_name = "test_broken_hwe_dir";
	return 0;
}

/*
 * Device section with a single regex entry ("^.foo:(bar|baz|ba\.)$")
 */
static void test_regex_hwe(const struct hwt_state *hwt)
{
	struct path *pp;

	/* foo:bar matches */
	pp = mock_path(vnd_foo.value, prd_bar.value);
	TEST_PROP(prio_name(&pp->prio), prio_emc.value);

	/* foo:baz matches */
	pp = mock_path(vnd_foo.value, prd_baz.value);
	TEST_PROP(prio_name(&pp->prio), prio_emc.value);

	/* boo:baz matches */
	pp = mock_path(vnd_boo.value, prd_bar.value);
	TEST_PROP(prio_name(&pp->prio), prio_emc.value);

	/* foo:BAR doesn't match */
	pp = mock_path(vnd_foo.value, "BAR");
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);

	/* bboo:bar doesn't match */
	pp = mock_path("bboo", prd_bar.value);
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);
}

static int setup_regex_hwe(void **state)
{
	struct hwt_state *hwt = CHECK_STATE(state);
	const struct key_value kv[] = { vnd_t_oo, prd_ba_s, prio_emc };

	WRITE_ONE_DEVICE(hwt, kv);
	SET_TEST_FUNC(hwt, test_regex_hwe);
	return 0;
}

/*
 * Two device entries, kv1 is a regex match ("^.foo:(bar|baz|ba\.)$"),
 * kv2 a string match (foo:bar) which matches a subset of the regex.
 * Both are added to the main config file.
 *
 * Expected: Devices matching both get properties from both, kv2 taking
 * precedence. Devices matching kv1 only just get props from kv1.
 */
static void test_regex_string_hwe(const struct hwt_state *hwt)
{
	struct path *pp;

	/* foo:baz matches kv1 */
	pp = mock_path(vnd_foo.value, prd_baz.value);
	TEST_PROP(prio_name(&pp->prio), prio_emc.value);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);

	/* boo:baz matches kv1 */
	pp = mock_path(vnd_boo.value, prd_baz.value);
	TEST_PROP(prio_name(&pp->prio), prio_emc.value);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);

	/* .oo:ba. matches kv1 */
	pp = mock_path(vnd__oo.value, prd_ba_.value);
	TEST_PROP(prio_name(&pp->prio), prio_emc.value);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);

	/* .foo:(bar|baz|ba\.) doesn't match */
	pp = mock_path(vnd__oo.value, prd_ba_s.value);
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), DEFAULT_CHECKER);

	/* foo:bar matches kv2 and kv1 */
	pp = mock_path_flags(vnd_foo.value, prd_bar.value, USE_GETUID);
	TEST_PROP(prio_name(&pp->prio), prio_hds.value);
	TEST_PROP(pp->getuid, gui_foo.value);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);
}

static int setup_regex_string_hwe(void **state)
{
	struct hwt_state *hwt = CHECK_STATE(state);
	const struct key_value kv1[] = { vnd_t_oo, prd_ba_s, prio_emc, chk_hp };
	const struct key_value kv2[] = { vnd_foo, prd_bar, prio_hds, gui_foo };

	WRITE_TWO_DEVICES(hwt, kv1, kv2);
	SET_TEST_FUNC(hwt, test_regex_string_hwe);
	return 0;
}

/*
 * Two device entries, kv1 is a regex match ("^.foo:(bar|baz|ba\.)$"),
 * kv2 a string match (foo:bar) which matches a subset of the regex.
 * kv1 is added to the main config file, kv2 to a config_dir file.
 * This case is more important as you may think, because it's equivalent
 * to kv1 being in the built-in hwtable and kv2 in multipath.conf.
 *
 * Expected: Devices matching kv2 (and thus, both) get properties
 * from both, kv2 taking precedence.
 * Devices matching kv1 only just get props from kv1.
 */
static void test_regex_string_hwe_dir(const struct hwt_state *hwt)
{
	struct path *pp;

	/* foo:baz matches kv1 */
	pp = mock_path(vnd_foo.value, prd_baz.value);
	TEST_PROP(prio_name(&pp->prio), prio_emc.value);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);

	/* boo:baz matches kv1 */
	pp = mock_path(vnd_boo.value, prd_baz.value);
	TEST_PROP(prio_name(&pp->prio), prio_emc.value);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);

	/* .oo:ba. matches kv1 */
	pp = mock_path(vnd__oo.value, prd_ba_.value);
	TEST_PROP(prio_name(&pp->prio), prio_emc.value);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);

	/* .oo:(bar|baz|ba\.)$ doesn't match */
	pp = mock_path(vnd__oo.value, prd_ba_s.value);
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), DEFAULT_CHECKER);

	/* foo:bar matches kv2 */
	pp = mock_path_flags(vnd_foo.value, prd_bar.value, USE_GETUID);
	/* Later match takes prio */
	TEST_PROP(prio_name(&pp->prio), prio_hds.value);
	TEST_PROP(pp->getuid, gui_foo.value);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);
}

static int setup_regex_string_hwe_dir(void **state)
{
	const struct key_value kv1[] = { vnd_t_oo, prd_ba_s, prio_emc, chk_hp };
	const struct key_value kv2[] = { vnd_foo, prd_bar, prio_hds, gui_foo };
	struct hwt_state *hwt = CHECK_STATE(state);

	WRITE_TWO_DEVICES_W_DIR(hwt, kv1, kv2);
	SET_TEST_FUNC(hwt, test_regex_string_hwe_dir);
	return 0;
}

/*
 * Three device entries, kv1 is a regex match and kv2 and kv3 string
 * matches, where kv3 is a substring of kv2. All in different config
 * files.
 *
 * Expected: Devices matching kv3 get props from all, devices matching
 * kv2 from kv2 and kv1, and devices matching kv1 only just from kv1.
 */
static void test_regex_2_strings_hwe_dir(const struct hwt_state *hwt)
{
	struct path *pp;

	/* foo:baz matches kv1 */
	pp = mock_path(vnd_foo.value, prd_baz.value);
	TEST_PROP(prio_name(&pp->prio), prio_emc.value);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(pp->uid_attribute, DEFAULT_UID_ATTRIBUTE);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);

	/* boo:baz doesn't match */
	pp = mock_path(vnd_boo.value, prd_baz.value);
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(pp->uid_attribute, DEFAULT_UID_ATTRIBUTE);
	TEST_PROP(checker_name(&pp->checker), DEFAULT_CHECKER);

	/* foo:bar matches kv2 and kv1 */
	pp = mock_path(vnd_foo.value, prd_bar.value);
	TEST_PROP(prio_name(&pp->prio), prio_hds.value);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(pp->uid_attribute, uid_baz.value);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);

	/* foo:barz matches kv3 and kv2 and kv1 */
	pp = mock_path_flags(vnd_foo.value, prd_barz.value, USE_GETUID);
	TEST_PROP(prio_name(&pp->prio), prio_rdac.value);
	TEST_PROP(pp->getuid, gui_foo.value);
	TEST_PROP(pp->uid_attribute, NULL);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);
}

static int setup_regex_2_strings_hwe_dir(void **state)
{
	const struct key_value kv1[] = { vnd_foo, prd_ba_, prio_emc, chk_hp };
	const struct key_value kv2[] = { vnd_foo, prd_bar, prio_hds, uid_baz };
	const struct key_value kv3[] = { vnd_foo, prd_barz,
					 prio_rdac, gui_foo };
	struct hwt_state *hwt = CHECK_STATE(state);

	begin_config(hwt);
	begin_section_all(hwt, "devices");
	write_device(hwt->config_file, ARRAY_SIZE(kv1), kv1);
	write_device(hwt->conf_dir_file[0], ARRAY_SIZE(kv2), kv2);
	write_device(hwt->conf_dir_file[1], ARRAY_SIZE(kv3), kv3);
	end_section_all(hwt);
	finish_config(hwt);
	SET_TEST_FUNC(hwt, test_regex_2_strings_hwe_dir);
	return 0;
}

/*
 * Like test_regex_string_hwe_dir, but the order of kv1 and kv2 is exchanged.
 *
 * Expected: Devices matching kv1 (and thus, both) get properties
 * from both, kv1 taking precedence.
 * Devices matching kv1 only just get props from kv1.
 */
static void test_string_regex_hwe_dir(const struct hwt_state *hwt)
{
	struct path *pp;

	/* foo:bar matches kv2 and kv1 */
	pp = mock_path_flags(vnd_foo.value, prd_bar.value, USE_GETUID);
	TEST_PROP(prio_name(&pp->prio), prio_emc.value);
	TEST_PROP(pp->getuid, gui_foo.value);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);

	/* foo:baz matches kv1 */
	pp = mock_path(vnd_foo.value, prd_baz.value);
	TEST_PROP(prio_name(&pp->prio), prio_emc.value);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);

	/* boo:baz matches kv1 */
	pp = mock_path(vnd_boo.value, prd_baz.value);
	TEST_PROP(prio_name(&pp->prio), prio_emc.value);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);

	/* .oo:ba. matches kv1 */
	pp = mock_path(vnd__oo.value, prd_ba_.value);
	TEST_PROP(prio_name(&pp->prio), prio_emc.value);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);

	/* .oo:(bar|baz|ba\.)$ doesn't match */
	pp = mock_path(vnd__oo.value, prd_ba_s.value);
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), DEFAULT_CHECKER);
}

static int setup_string_regex_hwe_dir(void **state)
{
	const struct key_value kv1[] = { vnd_t_oo, prd_ba_s, prio_emc, chk_hp };
	const struct key_value kv2[] = { vnd_foo, prd_bar, prio_hds, gui_foo };
	struct hwt_state *hwt = CHECK_STATE(state);

	WRITE_TWO_DEVICES_W_DIR(hwt, kv2, kv1);
	SET_TEST_FUNC(hwt, test_string_regex_hwe_dir);
	return 0;
}

/*
 * Two identical device entries kv1 and kv2, trival regex ("string").
 * Both are added to the main config file.
 * These entries are NOT merged.
 * This could happen in a large multipath.conf file.
 *
 * Expected: matching devices get props from both, kv2 taking precedence.
 */
static void test_2_ident_strings_hwe(const struct hwt_state *hwt)
{
	struct path *pp;

	/* foo:baz doesn't match */
	pp = mock_path(vnd_foo.value, prd_baz.value);
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), DEFAULT_CHECKER);

	/* foo:bar matches both */
	pp = mock_path_flags(vnd_foo.value, prd_bar.value, USE_GETUID);
	TEST_PROP(prio_name(&pp->prio), prio_hds.value);
	TEST_PROP(pp->getuid, gui_foo.value);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);
}

static int setup_2_ident_strings_hwe(void **state)
{
	const struct key_value kv1[] = { vnd_foo, prd_bar, prio_emc, chk_hp };
	const struct key_value kv2[] = { vnd_foo, prd_bar, prio_hds, gui_foo };
	struct hwt_state *hwt = CHECK_STATE(state);

	WRITE_TWO_DEVICES(hwt, kv1, kv2);
	SET_TEST_FUNC(hwt, test_2_ident_strings_hwe);
	return 0;
}

/*
 * Two identical device entries kv1 and kv2, trival regex ("string").
 * Both are added to an extra config file.
 * This could happen in a large multipath.conf file.
 *
 * Expected: matching devices get props from both, kv2 taking precedence.
 */
static void test_2_ident_strings_both_dir(const struct hwt_state *hwt)
{
	struct path *pp;

	/* foo:baz doesn't match */
	pp = mock_path(vnd_foo.value, prd_baz.value);
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), DEFAULT_CHECKER);

	/* foo:bar matches both */
	pp = mock_path_flags(vnd_foo.value, prd_bar.value, USE_GETUID);
	TEST_PROP(prio_name(&pp->prio), prio_hds.value);
	TEST_PROP(pp->getuid, gui_foo.value);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);
}

static int setup_2_ident_strings_both_dir(void **state)
{
	const struct key_value kv1[] = { vnd_foo, prd_bar, prio_emc, chk_hp };
	const struct key_value kv2[] = { vnd_foo, prd_bar, prio_hds, gui_foo };
	struct hwt_state *hwt = CHECK_STATE(state);

	begin_config(hwt);
	begin_section_all(hwt, "devices");
	write_device(hwt->conf_dir_file[1], ARRAY_SIZE(kv1), kv1);
	write_device(hwt->conf_dir_file[1], ARRAY_SIZE(kv2), kv2);
	end_section_all(hwt);
	finish_config(hwt);
	SET_TEST_FUNC(hwt, test_2_ident_strings_both_dir);
	return 0;
}

/*
 * Two identical device entries kv1 and kv2, trival regex ("string").
 * Both are added to an extra config file.
 * An empty entry kv0 with the same string exists in the main config file.
 *
 * Expected: matching devices get props from both, kv2 taking precedence.
 */
static void test_2_ident_strings_both_dir_w_prev(const struct hwt_state *hwt)
{
	struct path *pp;

	/* foo:baz doesn't match */
	pp = mock_path(vnd_foo.value, prd_baz.value);
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), DEFAULT_CHECKER);

	/* foo:bar matches both */
	pp = mock_path_flags(vnd_foo.value, prd_bar.value, USE_GETUID);
	TEST_PROP(prio_name(&pp->prio), prio_hds.value);
	TEST_PROP(pp->getuid, gui_foo.value);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);
}

static int setup_2_ident_strings_both_dir_w_prev(void **state)
{
	struct hwt_state *hwt = CHECK_STATE(state);

	const struct key_value kv0[] = { vnd_foo, prd_bar };
	const struct key_value kv1[] = { vnd_foo, prd_bar, prio_emc, chk_hp };
	const struct key_value kv2[] = { vnd_foo, prd_bar, prio_hds, gui_foo };

	begin_config(hwt);
	begin_section_all(hwt, "devices");
	write_device(hwt->config_file, ARRAY_SIZE(kv0), kv0);
	write_device(hwt->conf_dir_file[1], ARRAY_SIZE(kv1), kv1);
	write_device(hwt->conf_dir_file[1], ARRAY_SIZE(kv2), kv2);
	end_section_all(hwt);
	finish_config(hwt);
	SET_TEST_FUNC(hwt, test_2_ident_strings_both_dir_w_prev);
	return 0;
}

/*
 * Two identical device entries kv1 and kv2, trival regex ("string").
 * kv1 is added to the main config file, kv2 to a config_dir file.
 * These entries are merged.
 * This case is more important as you may think, because it's equivalent
 * to kv1 being in the built-in hwtable and kv2 in multipath.conf.
 *
 * Expected: matching devices get props from both, kv2 taking precedence.
 */
static void test_2_ident_strings_hwe_dir(const struct hwt_state *hwt)
{
	struct path *pp;

	/* foo:baz doesn't match */
	pp = mock_path(vnd_foo.value, prd_baz.value);
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), DEFAULT_CHECKER);

	/* foo:bar matches both */
	pp = mock_path_flags(vnd_foo.value, prd_bar.value, USE_GETUID);
	TEST_PROP(prio_name(&pp->prio), prio_hds.value);
	TEST_PROP(pp->getuid, gui_foo.value);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);
}

static int setup_2_ident_strings_hwe_dir(void **state)
{
	const struct key_value kv1[] = { vnd_foo, prd_bar, prio_emc, chk_hp };
	const struct key_value kv2[] = { vnd_foo, prd_bar, prio_hds, gui_foo };
	struct hwt_state *hwt = CHECK_STATE(state);

	WRITE_TWO_DEVICES_W_DIR(hwt, kv1, kv2);
	SET_TEST_FUNC(hwt, test_2_ident_strings_hwe_dir);
	return 0;
}

/*
 * Like test_2_ident_strings_hwe_dir, but this time the config_dir file
 * contains an additional, empty entry (kv0).
 *
 * Expected: matching devices get props from kv1 and kv2, kv2 taking precedence.
 */
static void test_3_ident_strings_hwe_dir(const struct hwt_state *hwt)
{
	struct path *pp;

	/* foo:baz doesn't match */
	pp = mock_path(vnd_foo.value, prd_baz.value);
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), DEFAULT_CHECKER);

	/* foo:bar matches both */
	pp = mock_path_flags(vnd_foo.value, prd_bar.value, USE_GETUID);
	TEST_PROP(prio_name(&pp->prio), prio_hds.value);
	TEST_PROP(pp->getuid, gui_foo.value);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);
}

static int setup_3_ident_strings_hwe_dir(void **state)
{
	const struct key_value kv0[] = { vnd_foo, prd_bar };
	const struct key_value kv1[] = { vnd_foo, prd_bar, prio_emc, chk_hp };
	const struct key_value kv2[] = { vnd_foo, prd_bar, prio_hds, gui_foo };
	struct hwt_state *hwt = CHECK_STATE(state);

	begin_config(hwt);
	begin_section_all(hwt, "devices");
	write_device(hwt->config_file, ARRAY_SIZE(kv1), kv1);
	write_device(hwt->conf_dir_file[1], ARRAY_SIZE(kv0), kv0);
	write_device(hwt->conf_dir_file[1], ARRAY_SIZE(kv2), kv2);
	end_section_all(hwt);
	finish_config(hwt);
	SET_TEST_FUNC(hwt, test_3_ident_strings_hwe_dir);
	return 0;
}

/*
 * Two identical device entries kv1 and kv2, non-trival regex that matches
 * itself (string ".oo" matches regex ".oo").
 * kv1 is added to the main config file, kv2 to a config_dir file.
 * This case is more important as you may think, because it's equivalent
 * to kv1 being in the built-in hwtable and kv2 in multipath.conf.
 *
 * Expected: matching devices get props from both, kv2 taking precedence.
 */
static void test_2_ident_self_matching_re_hwe_dir(const struct hwt_state *hwt)
{
	struct path *pp;

	/* foo:baz doesn't match */
	pp = mock_path(vnd_foo.value, prd_baz.value);
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), DEFAULT_CHECKER);

	/* foo:bar matches both */
	pp = mock_path_flags(vnd_foo.value, prd_bar.value, USE_GETUID);
	TEST_PROP(prio_name(&pp->prio), prio_hds.value);
	TEST_PROP(pp->getuid, gui_foo.value);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);
}

static int setup_2_ident_self_matching_re_hwe_dir(void **state)
{
	const struct key_value kv1[] = { vnd__oo, prd_bar, prio_emc, chk_hp };
	const struct key_value kv2[] = { vnd__oo, prd_bar, prio_hds, gui_foo };
	struct hwt_state *hwt = CHECK_STATE(state);

	WRITE_TWO_DEVICES_W_DIR(hwt, kv1, kv2);
	SET_TEST_FUNC(hwt, test_2_ident_self_matching_re_hwe_dir);
	return 0;
}

/*
 * Two identical device entries kv1 and kv2, non-trival regex that matches
 * itself (string ".oo" matches regex ".oo").
 * kv1 and kv2 are added to the main config file.
 *
 * Expected: matching devices get props from both, kv2 taking precedence.
 */
static void test_2_ident_self_matching_re_hwe(const struct hwt_state *hwt)
{
	struct path *pp;

	/* foo:baz doesn't match */
	pp = mock_path(vnd_foo.value, prd_baz.value);
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), DEFAULT_CHECKER);

	/* foo:bar matches */
	pp = mock_path_flags(vnd_foo.value, prd_bar.value, USE_GETUID);
	TEST_PROP(prio_name(&pp->prio), prio_hds.value);
	TEST_PROP(pp->getuid, gui_foo.value);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);
}

static int setup_2_ident_self_matching_re_hwe(void **state)
{
	const struct key_value kv1[] = { vnd__oo, prd_bar, prio_emc, chk_hp };
	const struct key_value kv2[] = { vnd__oo, prd_bar, prio_hds, gui_foo };
	struct hwt_state *hwt = CHECK_STATE(state);

	WRITE_TWO_DEVICES(hwt, kv1, kv2);
	SET_TEST_FUNC(hwt, test_2_ident_self_matching_re_hwe);
	return 0;
}

/*
 * Two identical device entries kv1 and kv2, non-trival regex that doesn't
 * match itself (string "^.oo" doesn't match regex "^.oo").
 * kv1 is added to the main config file, kv2 to a config_dir file.
 * This case is more important as you may think, see above.
 *
 * Expected: matching devices get props from both, kv2 taking precedence.
 */
static void
test_2_ident_not_self_matching_re_hwe_dir(const struct hwt_state *hwt)
{
	struct path *pp;

	/* foo:baz doesn't match */
	pp = mock_path(vnd_foo.value, prd_baz.value);
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), DEFAULT_CHECKER);

	/* foo:bar matches both */
	pp = mock_path_flags(vnd_foo.value, prd_bar.value, USE_GETUID);
	TEST_PROP(prio_name(&pp->prio), prio_hds.value);
	TEST_PROP(pp->getuid, gui_foo.value);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);
}

static int setup_2_ident_not_self_matching_re_hwe_dir(void **state)
{
	const struct key_value kv1[] = { vnd_t_oo, prd_bar, prio_emc, chk_hp };
	const struct key_value kv2[] = { vnd_t_oo, prd_bar, prio_hds, gui_foo };
	struct hwt_state *hwt = CHECK_STATE(state);

	WRITE_TWO_DEVICES_W_DIR(hwt, kv1, kv2);
	SET_TEST_FUNC(hwt, test_2_ident_not_self_matching_re_hwe_dir);
	return 0;
}

/*
 * Two different non-trivial regexes kv1, kv2. The 1st one matches the 2nd, but
 * it doesn't match all possible strings matching the second.
 * ("ba[zy]" matches regex "ba[[rxy]", but "baz" does not).
 *
 * Expected: Devices matching both regexes get properties from both, kv2
 * taking precedence. Devices matching just one regex get properties from
 * that one regex only.
 */
static void test_2_matching_res_hwe_dir(const struct hwt_state *hwt)
{
	struct path *pp;

	/* foo:bar matches k1 only */
	pp = mock_path(vnd_foo.value, prd_bar.value);
	TEST_PROP(prio_name(&pp->prio), prio_emc.value);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);

	/* foo:bay matches k1 and k2 */
	pp = mock_path_flags(vnd_foo.value, "bay", USE_GETUID);
	TEST_PROP(prio_name(&pp->prio), prio_hds.value);
	TEST_PROP(pp->getuid, gui_foo.value);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);

	/* foo:baz matches k2 only. */
	pp = mock_path_flags(vnd_foo.value, prd_baz.value, USE_GETUID);
	TEST_PROP(prio_name(&pp->prio), prio_hds.value);
	TEST_PROP(pp->getuid, gui_foo.value);
	TEST_PROP(checker_name(&pp->checker), DEFAULT_CHECKER);
}

static int setup_2_matching_res_hwe_dir(void **state)
{
	const struct key_value kv1[] = { vnd_foo, prd_barx, prio_emc, chk_hp };
	const struct key_value kv2[] = { vnd_foo, prd_bazy, prio_hds, gui_foo };
	struct hwt_state *hwt = CHECK_STATE(state);

	WRITE_TWO_DEVICES_W_DIR(hwt, kv1, kv2);
	SET_TEST_FUNC(hwt, test_2_matching_res_hwe_dir);
	return 0;
}

/*
 * Two different non-trivial regexes which match the same set of strings.
 * But they don't match each other.
 * "baz" matches both regex "ba[zy]" and "ba(z|y)"
 *
 * Expected: matching devices get properties from both, kv2 taking precedence.
 */
static void test_2_nonmatching_res_hwe_dir(const struct hwt_state *hwt)
{
	struct path *pp;

	/* foo:bar doesn't match */
	pp = mock_path(vnd_foo.value, prd_bar.value);
	TEST_PROP(prio_name(&pp->prio), DEFAULT_PRIO);
	TEST_PROP(pp->getuid, NULL);
	TEST_PROP(checker_name(&pp->checker), DEFAULT_CHECKER);

	pp = mock_path_flags(vnd_foo.value, prd_baz.value, USE_GETUID);
	TEST_PROP(prio_name(&pp->prio), prio_hds.value);
	TEST_PROP(pp->getuid, gui_foo.value);
	TEST_PROP(checker_name(&pp->checker), chk_hp.value);
}

static int setup_2_nonmatching_res_hwe_dir(void **state)
{
	const struct key_value kv1[] = { vnd_foo, prd_bazy, prio_emc, chk_hp };
	const struct key_value kv2[] = { vnd_foo, prd_bazy1,
					 prio_hds, gui_foo };
	struct hwt_state *hwt = CHECK_STATE(state);

	WRITE_TWO_DEVICES_W_DIR(hwt, kv1, kv2);
	SET_TEST_FUNC(hwt, test_2_nonmatching_res_hwe_dir);
	return 0;
}

/*
 * Simple blacklist test.
 *
 * NOTE: test failures in blacklisting tests will manifest as cmocka errors
 * "Could not get value to mock function XYZ", because pathinfo() takes
 * different code paths for blacklisted devices.
 */
static void test_blacklist(const struct hwt_state *hwt)
{
	mock_path_flags(vnd_foo.value, prd_bar.value, BL_BY_DEVICE);
	mock_path(vnd_foo.value, prd_baz.value);
}

static int setup_blacklist(void **state)
{
	const struct key_value kv1[] = { vnd_foo, prd_bar };
	struct hwt_state *hwt = CHECK_STATE(state);

	begin_config(hwt);
	begin_section_all(hwt, "blacklist");
	write_device(hwt->config_file, ARRAY_SIZE(kv1), kv1);
	end_section_all(hwt);
	finish_config(hwt);
	SET_TEST_FUNC(hwt, test_blacklist);
	return 0;
}

/*
 * Simple blacklist test with regex and exception
 */
static void test_blacklist_regex(const struct hwt_state *hwt)
{
	mock_path(vnd_foo.value, prd_bar.value);
	mock_path_flags(vnd_foo.value, prd_baz.value, BL_BY_DEVICE);
	mock_path(vnd_foo.value, prd_bam.value);
}

static int setup_blacklist_regex(void **state)
{
	const struct key_value kv1[] = { vnd_foo, prd_ba_s };
	const struct key_value kv2[] = { vnd_foo, prd_bar };
	struct hwt_state *hwt = CHECK_STATE(state);

	hwt = CHECK_STATE(state);
	begin_config(hwt);
	begin_section_all(hwt, "blacklist");
	write_device(hwt->config_file, ARRAY_SIZE(kv1), kv1);
	end_section_all(hwt);
	begin_section_all(hwt, "blacklist_exceptions");
	write_device(hwt->conf_dir_file[0], ARRAY_SIZE(kv2), kv2);
	end_section_all(hwt);
	finish_config(hwt);
	SET_TEST_FUNC(hwt, test_blacklist_regex);
	return 0;
}

/*
 * Simple blacklist test with regex and exception
 * config file order inverted wrt test_blacklist_regex
 */
static int setup_blacklist_regex_inv(void **state)
{
	const struct key_value kv1[] = { vnd_foo, prd_ba_s };
	const struct key_value kv2[] = { vnd_foo, prd_bar };
	struct hwt_state *hwt = CHECK_STATE(state);

	begin_config(hwt);
	begin_section_all(hwt, "blacklist");
	write_device(hwt->conf_dir_file[0], ARRAY_SIZE(kv1), kv1);
	end_section_all(hwt);
	begin_section_all(hwt, "blacklist_exceptions");
	write_device(hwt->config_file, ARRAY_SIZE(kv2), kv2);
	end_section_all(hwt);
	finish_config(hwt);
	SET_TEST_FUNC(hwt, test_blacklist_regex);
	return 0;
}

/*
 * Simple blacklist test with regex and exception
 * config file order inverted wrt test_blacklist_regex
 */
static void test_blacklist_regex_matching(const struct hwt_state *hwt)
{
	mock_path_flags(vnd_foo.value, prd_bar.value, BL_BY_DEVICE);
	mock_path_flags(vnd_foo.value, prd_baz.value, BL_BY_DEVICE);
	mock_path(vnd_foo.value, prd_bam.value);
}

static int setup_blacklist_regex_matching(void **state)
{
	const struct key_value kv1[] = { vnd_foo, prd_barx };
	const struct key_value kv2[] = { vnd_foo, prd_bazy };
	struct hwt_state *hwt = CHECK_STATE(state);

	begin_config(hwt);
	begin_section_all(hwt, "blacklist");
	write_device(hwt->config_file, ARRAY_SIZE(kv1), kv1);
	write_device(hwt->conf_dir_file[0], ARRAY_SIZE(kv2), kv2);
	end_section_all(hwt);
	finish_config(hwt);
	SET_TEST_FUNC(hwt, test_blacklist_regex_matching);
	return 0;
}

/*
 * Test for blacklisting by WWID
 *
 * Note that default_wwid is a substring of default_wwid_1. Because
 * matching is done by regex, both paths are blacklisted.
 */
static void test_blacklist_wwid(const struct hwt_state *hwt)
{
	mock_path_flags(vnd_foo.value, prd_bar.value, BL_BY_WWID);
	mock_path_wwid_flags(vnd_foo.value, prd_baz.value, default_wwid_1,
			     BL_BY_WWID);
}

static int setup_blacklist_wwid(void **state)
{
	const struct key_value kv[] = { wwid_test };
	struct hwt_state *hwt = CHECK_STATE(state);

	begin_config(hwt);
	write_section(hwt->config_file, "blacklist", ARRAY_SIZE(kv), kv);
	finish_config(hwt);
	SET_TEST_FUNC(hwt, test_blacklist_wwid);
	return 0;
}

/*
 * Test for blacklisting by WWID
 *
 * Here the blacklist contains only default_wwid_1. Thus the path
 * with default_wwid is NOT blacklisted.
 */
static void test_blacklist_wwid_1(const struct hwt_state *hwt)
{
	mock_path(vnd_foo.value, prd_bar.value);
	mock_path_wwid_flags(vnd_foo.value, prd_baz.value, default_wwid_1,
			     BL_BY_WWID);
}

static int setup_blacklist_wwid_1(void **state)
{
	const struct key_value kv[] = { { _wwid, default_wwid_1 }, };
	struct hwt_state *hwt = CHECK_STATE(state);

	begin_config(hwt);
	write_section(hwt->config_file, "blacklist", ARRAY_SIZE(kv), kv);
	finish_config(hwt);
	SET_TEST_FUNC(hwt, test_blacklist_wwid_1);
	return 0;
}

/*
 * Test for product_blacklist. Two entries blacklisting each other.
 *
 * Expected: Both are blacklisted.
 */
static void test_product_blacklist(const struct hwt_state *hwt)
{
	mock_path_flags(vnd_foo.value, prd_baz.value, BL_BY_DEVICE);
	mock_path_flags(vnd_foo.value, prd_bar.value, BL_BY_DEVICE);
	mock_path(vnd_foo.value, prd_bam.value);
}

static int setup_product_blacklist(void **state)
{
	const struct key_value kv1[] = { vnd_foo, prd_bar, bl_baz };
	const struct key_value kv2[] = { vnd_foo, prd_baz, bl_bar };
	struct hwt_state *hwt = CHECK_STATE(state);

	WRITE_TWO_DEVICES(hwt, kv1, kv2);
	SET_TEST_FUNC(hwt, test_product_blacklist);
	return 0;
}

/*
 * Test for product_blacklist. The second regex "matches" the first.
 * This is a pathological example.
 *
 * Expected: "foo:bar", "foo:baz" are blacklisted.
 */
static void test_product_blacklist_matching(const struct hwt_state *hwt)
{
	mock_path_flags(vnd_foo.value, prd_bar.value, BL_BY_DEVICE);
	mock_path_flags(vnd_foo.value, prd_baz.value, BL_BY_DEVICE);
	mock_path(vnd_foo.value, prd_bam.value);
}

static int setup_product_blacklist_matching(void **state)
{
	const struct key_value kv1[] = { vnd_foo, prd_bar, bl_barx };
	const struct key_value kv2[] = { vnd_foo, prd_baz, bl_bazy };
	struct hwt_state *hwt = CHECK_STATE(state);

	WRITE_TWO_DEVICES(hwt, kv1, kv2);
	SET_TEST_FUNC(hwt, test_product_blacklist_matching);
	return 0;
}

/*
 * Basic test for multipath-based configuration.
 *
 * Expected: properties, including pp->prio, are taken from multipath
 * section.
 */
static void test_multipath_config(const struct hwt_state *hwt)
{
	struct path *pp;
	struct multipath *mp;

	pp = mock_path(vnd_foo.value, prd_bar.value);
	mp = mock_multipath(pp);
	assert_ptr_not_equal(mp->mpe, NULL);
	TEST_PROP(prio_name(&pp->prio), prio_rdac.value);
	assert_int_equal(mp->minio, atoi(minio_99.value));
	TEST_PROP(pp->uid_attribute, uid_baz.value);

	/* test different wwid */
	pp = mock_path_wwid(vnd_foo.value, prd_bar.value, default_wwid_1);
	mp = mock_multipath(pp);
	// assert_ptr_equal(mp->mpe, NULL);
	TEST_PROP(prio_name(&pp->prio), prio_emc.value);
	assert_int_equal(mp->minio, DEFAULT_MINIO_RQ);
	TEST_PROP(pp->uid_attribute, uid_baz.value);
}

static int setup_multipath_config(void **state)
{
	struct hwt_state *hwt = CHECK_STATE(state);
	const struct key_value kvm[] = { wwid_test, prio_rdac, minio_99 };
	const struct key_value kvp[] = { vnd_foo, prd_bar, prio_emc, uid_baz };

	begin_config(hwt);
	begin_section_all(hwt, "devices");
	write_section(hwt->conf_dir_file[0], "device", ARRAY_SIZE(kvp), kvp);
	end_section_all(hwt);
	begin_section_all(hwt, "multipaths");
	write_section(hwt->config_file, "multipath", ARRAY_SIZE(kvm), kvm);
	end_section_all(hwt);
	finish_config(hwt);
	SET_TEST_FUNC(hwt, test_multipath_config);
	return 0;
}

/*
 * Basic test for multipath-based configuration. Two sections for the same wwid.
 *
 * Expected: properties are taken from both multipath sections, later taking
 * precedence
 */
static void test_multipath_config_2(const struct hwt_state *hwt)
{
	struct path *pp;
	struct multipath *mp;

	pp = mock_path(vnd_foo.value, prd_bar.value);
	mp = mock_multipath(pp);
	assert_ptr_not_equal(mp, NULL);
	assert_ptr_not_equal(mp->mpe, NULL);
	TEST_PROP(prio_name(&pp->prio), prio_rdac.value);
	assert_int_equal(mp->minio, atoi(minio_99.value));
	assert_int_equal(mp->no_path_retry, atoi(npr_37.value));
}

static int setup_multipath_config_2(void **state)
{
	const struct key_value kv1[] = { wwid_test, prio_rdac, npr_queue };
	const struct key_value kv2[] = { wwid_test, minio_99, npr_37 };
	struct hwt_state *hwt = CHECK_STATE(state);

	begin_config(hwt);
	begin_section_all(hwt, "multipaths");
	write_section(hwt->config_file, "multipath", ARRAY_SIZE(kv1), kv1);
	write_section(hwt->conf_dir_file[1], "multipath", ARRAY_SIZE(kv2), kv2);
	end_section_all(hwt);
	finish_config(hwt);
	SET_TEST_FUNC(hwt, test_multipath_config_2);
	return 0;
}

/*
 * Same as test_multipath_config_2, both entries in the same config file.
 *
 * Expected: properties are taken from both multipath sections.
 */
static void test_multipath_config_3(const struct hwt_state *hwt)
{
	struct path *pp;
	struct multipath *mp;

	pp = mock_path(vnd_foo.value, prd_bar.value);
	mp = mock_multipath(pp);
	assert_ptr_not_equal(mp, NULL);
	assert_ptr_not_equal(mp->mpe, NULL);
	TEST_PROP(prio_name(&pp->prio), prio_rdac.value);
	assert_int_equal(mp->minio, atoi(minio_99.value));
	assert_int_equal(mp->no_path_retry, atoi(npr_37.value));
}

static int setup_multipath_config_3(void **state)
{
	const struct key_value kv1[] = { wwid_test, prio_rdac, npr_queue };
	const struct key_value kv2[] = { wwid_test, minio_99, npr_37 };
	struct hwt_state *hwt = CHECK_STATE(state);

	begin_config(hwt);
	begin_section_all(hwt, "multipaths");
	write_section(hwt->config_file, "multipath", ARRAY_SIZE(kv1), kv1);
	write_section(hwt->config_file, "multipath", ARRAY_SIZE(kv2), kv2);
	end_section_all(hwt);
	finish_config(hwt);
	SET_TEST_FUNC(hwt, test_multipath_config_3);
	return 0;
}

/*
 * Test for device with "hidden" attribute
 */
static void test_hidden(const struct hwt_state *hwt)
{
	mock_path_flags("NVME", "NoName", DEV_HIDDEN|BL_MASK);
}

static int setup_hidden(void **state)
{
	struct hwt_state *hwt = CHECK_STATE(state);

	WRITE_EMPTY_CONF(hwt);
	SET_TEST_FUNC(hwt, test_hidden);

	return 0;
}

/*
 * Create wrapper functions around test_driver() to avoid that cmocka
 * always uses the same test name. That makes it easier to read test results.
 */

#define define_test(x)				\
	static void run_##x(void **state)	\
	{					\
		return test_driver(state);	\
	}

define_test(string_hwe)
define_test(broken_hwe)
define_test(broken_hwe_dir)
define_test(quoted_hwe)
define_test(internal_nvme)
define_test(regex_hwe)
define_test(regex_string_hwe)
define_test(regex_string_hwe_dir)
define_test(regex_2_strings_hwe_dir)
define_test(string_regex_hwe_dir)
define_test(2_ident_strings_hwe)
define_test(2_ident_strings_both_dir)
define_test(2_ident_strings_both_dir_w_prev)
define_test(2_ident_strings_hwe_dir)
define_test(3_ident_strings_hwe_dir)
define_test(2_ident_self_matching_re_hwe_dir)
define_test(2_ident_self_matching_re_hwe)
define_test(2_ident_not_self_matching_re_hwe_dir)
define_test(2_matching_res_hwe_dir)
define_test(2_nonmatching_res_hwe_dir)
define_test(blacklist)
define_test(blacklist_wwid)
define_test(blacklist_wwid_1)
define_test(blacklist_regex)
define_test(blacklist_regex_inv)
define_test(blacklist_regex_matching)
define_test(product_blacklist)
define_test(product_blacklist_matching)
define_test(multipath_config)
define_test(multipath_config_2)
define_test(multipath_config_3)
define_test(hidden)

#define test_entry(x) \
	cmocka_unit_test_setup(run_##x, setup_##x)

static int test_hwtable(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_sanity_globals),
		test_entry(internal_nvme),
		test_entry(string_hwe),
		test_entry(broken_hwe),
		test_entry(broken_hwe_dir),
		test_entry(quoted_hwe),
		test_entry(regex_hwe),
		test_entry(regex_string_hwe),
		test_entry(regex_string_hwe_dir),
		test_entry(regex_2_strings_hwe_dir),
		test_entry(string_regex_hwe_dir),
		test_entry(2_ident_strings_hwe),
		test_entry(2_ident_strings_both_dir),
		test_entry(2_ident_strings_both_dir_w_prev),
		test_entry(2_ident_strings_hwe_dir),
		test_entry(3_ident_strings_hwe_dir),
		test_entry(2_ident_self_matching_re_hwe_dir),
		test_entry(2_ident_self_matching_re_hwe),
		test_entry(2_ident_not_self_matching_re_hwe_dir),
		test_entry(2_matching_res_hwe_dir),
		test_entry(2_nonmatching_res_hwe_dir),
		test_entry(blacklist),
		test_entry(blacklist_wwid),
		test_entry(blacklist_wwid_1),
		test_entry(blacklist_regex),
		test_entry(blacklist_regex_inv),
		test_entry(blacklist_regex_matching),
		test_entry(product_blacklist),
		test_entry(product_blacklist_matching),
		test_entry(multipath_config),
		test_entry(multipath_config_2),
		test_entry(multipath_config_3),
		test_entry(hidden),
	};

	return cmocka_run_group_tests(tests, setup, teardown);
}

int main(void)
{
	int ret = 0;

	ret += test_hwtable();
	return ret;
}
