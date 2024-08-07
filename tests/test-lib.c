#include <string.h>
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <libudev.h>
#include <sys/sysmacros.h>
#include <linux/hdreg.h>
#include <scsi/sg.h>
#include "debug.h"
#include "util.h"
#include "vector.h"
#include "structs.h"
#include "structs_vec.h"
#include "config.h"
#include "discovery.h"
#include "propsel.h"
#include "unaligned.h"
#include "test-lib.h"
#include "wrap64.h"

const int default_mask = (DI_SYSFS|DI_BLACKLIST|DI_WWID|DI_CHECKER|DI_PRIO|DI_SERIAL);
const char default_devnode[] = "sdxTEST";
const char default_wwid[] = "TEST-WWID";
/* default_wwid should be a substring of default_wwid_1! */
const char default_wwid_1[] = "TEST-WWID-1";

/*
 * Helper wrappers for mock_path().
 *
 * We need to make pathinfo() think it has detected a device with
 * certain vendor/product/rev. This requires faking lots of udev
 * and sysfs function responses.
 *
 * This requires hwtable-test_OBJDEPS = ../libmultipath/discovery.o
 * in the Makefile in order to wrap calls from discovery.o.
 *
 * Note that functions that are called and defined in discovery.o can't
 * be wrapped this way (e.g. sysfs_get_vendor), because symbols are
 * resolved by the assembler before the linking stage.
 */


int REAL_OPEN(const char *path, int flags, int mode);

static const char _mocked_filename[] = "mocked_path";

int WRAP_OPEN(const char *path, int flags, int mode)
{
	condlog(4, "%s: %s", __func__, path);

	if (!strcmp(path, _mocked_filename))
		return 111;
	return REAL_OPEN(path, flags, mode);
}

int __wrap_libmp_get_version(int which, unsigned int version[3])
{
	unsigned int *vers = mock_ptr_type(unsigned int *);

	condlog(4, "%s: %d", __func__, which);
	memcpy(version, vers, 3 * sizeof(unsigned int));
	return 0;
}

struct udev_list_entry
*__wrap_udev_device_get_properties_list_entry(struct udev_device *ud)
{
	void *p = (void*)0x12345678;
	condlog(5, "%s: %p", __func__, p);

	return p;
}

struct udev_list_entry
*__wrap_udev_list_entry_get_next(struct udev_list_entry *udle)
{
	void *p  = NULL;
	condlog(5, "%s: %p", __func__, p);

	return p;
}

const char *__wrap_udev_list_entry_get_name(struct udev_list_entry *udle)
{
	char *val = mock_ptr_type(char *);

	condlog(5, "%s: %s", __func__, val);
	return val;
}

struct udev_device *__wrap_udev_device_ref(struct udev_device *ud)
{
	return ud;
}

struct udev_device *__wrap_udev_device_unref(struct udev_device *ud)
{
	return ud;
}

char *__wrap_udev_device_get_subsystem(struct udev_device *ud)
{
	char *val = mock_ptr_type(char *);

	condlog(5, "%s: %s", __func__, val);
	return val;
}

char *__wrap_udev_device_get_sysname(struct udev_device *ud)
{
	char *val  = mock_ptr_type(char *);

	condlog(5, "%s: %s", __func__, val);
	return val;
}

char *__wrap_udev_device_get_devnode(struct udev_device *ud)
{
	char *val  = mock_ptr_type(char *);

	condlog(5, "%s: %s", __func__, val);
	return val;
}

dev_t __wrap_udev_device_get_devnum(struct udev_device *ud)
{
	condlog(5, "%s: %p", __func__, ud);
	return makedev(17, 17);
}

char *__wrap_udev_device_get_sysattr_value(struct udev_device *ud,
					     const char *attr)
{
	char *val  = mock_ptr_type(char *);

	condlog(5, "%s: %s->%s", __func__, attr, val);
	return val;
}

char *__wrap_udev_device_get_property_value(struct udev_device *ud,
					    const char *attr)
{
	char *val  = mock_ptr_type(char *);

	condlog(5, "%s: %s->%s", __func__, attr, val);
	return val;
}

int __wrap_sysfs_get_size(struct path *pp, unsigned long long *sz)
{
	*sz = 12345678UL;
	return 0;
}

int __wrap_sysfs_attr_set_value(struct udev_device *dev, const char *attr_name,
			   const char * value, size_t value_len)
{
	condlog(5, "%s: %s", __func__, value);
	return value_len;
}

void *__wrap_udev_device_get_parent_with_subsystem_devtype(
	struct udev_device *ud, const char *subsys, char *type)
{
	/* return non-NULL for sysfs_get_tgt_nodename */
	return type;
}

void *__wrap_udev_device_get_parent(struct udev_device *ud)
{
	char *val  = mock_ptr_type(void *);

	condlog(5, "%s: %p", __func__, val);
	return val;
}

ssize_t __wrap_sysfs_attr_get_value(struct udev_device *dev,
				    const char *attr_name,
				    char *value, size_t sz)
{
	char *val  = mock_ptr_type(char *);

	condlog(5, "%s: %s", __func__, val);
	strlcpy(value, val, sz);
	return strlen(value);
}

/* mock vpd_pg80 */
ssize_t __wrap_sysfs_bin_attr_get_value(struct udev_device *dev,
					const char *attr_name,
					char *buf, size_t sz)
{
	static const char serial[] = "mptest_serial";

	assert_string_equal(attr_name, "vpd_pg80");
	assert_in_range(sz, sizeof(serial) + 3, INT_MAX);
	memset(buf, 0, sizeof(serial) + 3);
	buf[1] = 0x80;
	put_unaligned_be16(sizeof(serial) - 1, &buf[2]);
	memcpy(&buf[4], serial, sizeof(serial) - 1);

	return sizeof(serial) + 3;
}

int __wrap_checker_check(struct checker *c, int st)
{
	condlog(5, "%s: %d", __func__, st);
	return st;
}

int __wrap_prio_getprio(struct prio *p, struct path *pp, unsigned int tmo)
{
	int pr = 5;

	condlog(5, "%s: %d", __func__, pr);
	return pr;
}

int __real_ioctl(int fd, unsigned long request, void *param);

int __wrap_ioctl(int fd, unsigned long request, void *param)
{
	condlog(5, "%s: %lu", __func__, request);

	if (request == HDIO_GETGEO) {
		static const struct hd_geometry geom = {
			.heads = 4, .sectors = 31, .cylinders = 64, .start = 0
		};
		memcpy(param, &geom, sizeof(geom));
		return 0;
	} else if (request == SG_IO) {
		/* mock hp3par special VPD */
		struct sg_io_hdr *hdr = param;
		static const char vpd_data[] = "VPD DATA";
		unsigned char *buf = hdr->dxferp;
		/* see vpd_vendor_pages in discovery.c */
		const int HP3PAR_VPD = 0xc0;

		if (hdr->interface_id == 'S' && hdr->cmdp[0] == 0x12
		    && (hdr->cmdp[1] & 1) == 1 && hdr->cmdp[2] == HP3PAR_VPD) {
			assert_in_range(hdr->dxfer_len,
					sizeof(vpd_data) + 3, INT_MAX);
			memset(buf, 0, hdr->dxfer_len);
			buf[1] = HP3PAR_VPD;
			put_unaligned_be16(sizeof(vpd_data), &buf[2]);
			memcpy(&buf[4], vpd_data, sizeof(vpd_data));
			hdr->status = 0;
			return 0;
		}
	}
	return __real_ioctl(fd, request, param);
}

struct mocked_path *fill_mocked_path(struct mocked_path *mp,
				     const char *vendor, const char *product,
				     const char *rev, const char *wwid,
				     const char *devnode, unsigned int flags)
{
	mp->vendor = (vendor ? vendor : "noname");
	mp->product = (product ? product : "noprod");
	mp->rev = (rev ? rev : "0");
	mp->wwid = (wwid ? wwid : default_wwid);
	mp->devnode = (devnode ? devnode : default_devnode);
	mp->flags = flags|NEED_SELECT_PRIO|NEED_FD;
	return mp;
}

struct mocked_path *mocked_path_from_path(struct mocked_path *mp,
					  const struct path *pp)
{
	mp->vendor = pp->vendor_id;
	mp->product = pp->product_id;
	mp->rev = pp->rev;
	mp->wwid = pp->wwid;
	mp->devnode = pp->dev;
	mp->flags = (prio_selected(&pp->prio) ? 0 : NEED_SELECT_PRIO) |
		(pp->fd < 0 ? NEED_FD : 0) |
		(pp->vpd_vendor_id != 0 ? USE_VPD_VND : 0);
	return mp;
}

static const char hbtl[] = "4:0:3:1";
static void mock_sysfs_pathinfo(const struct mocked_path *mp)
{
	will_return(__wrap_udev_device_get_subsystem, "scsi");
	will_return(__wrap_udev_device_get_sysname, hbtl);
	will_return(__wrap_udev_device_get_sysname, hbtl);
	will_return(__wrap_udev_device_get_sysattr_value, mp->vendor);
	will_return(__wrap_udev_device_get_sysname, hbtl);
	will_return(__wrap_udev_device_get_sysattr_value, mp->product);
	will_return(__wrap_udev_device_get_sysname, hbtl);
	will_return(__wrap_udev_device_get_sysattr_value, mp->rev);

	/* sysfs_get_tgt_nodename */
	will_return(__wrap_udev_device_get_sysattr_value, NULL);
	will_return(__wrap_udev_device_get_parent, NULL);
	will_return(__wrap_udev_device_get_parent, NULL);
	will_return(__wrap_udev_device_get_sysname, "nofibre");
	will_return(__wrap_udev_device_get_sysname, "noiscsi");
	will_return(__wrap_udev_device_get_parent, NULL);
	will_return(__wrap_udev_device_get_sysname, "ata25");
}

/*
 * Pretend we detected a SCSI device with given vendor/prod/rev
 */
void mock_pathinfo(int mask, const struct mocked_path *mp)
{
	if (mp->flags & DEV_HIDDEN) {
		will_return(__wrap_udev_device_get_sysattr_value, "1");
		return;
	} else
		will_return(__wrap_udev_device_get_sysattr_value, "0");

	if (mask & DI_SYSFS)
		mock_sysfs_pathinfo(mp);

	if (mask & DI_BLACKLIST) {
		will_return(__wrap_udev_device_get_sysname, mp->devnode);
		if (mp->flags & BL_BY_PROPERTY) {
			will_return(__wrap_udev_list_entry_get_name, "BAZ");
			return;
		} else
			will_return(__wrap_udev_list_entry_get_name,
				    "SCSI_IDENT_LUN_NAA_EXT");
	}

	if (mp->flags & BL_BY_DEVICE &&
	    (mask & DI_BLACKLIST && mask & DI_SYSFS))
		return;

	/* path_offline */
	will_return(__wrap_udev_device_get_subsystem, "scsi");
	will_return(__wrap_sysfs_attr_get_value, "running");

	if (mask & DI_NOIO)
		return;

	/* fake open() in pathinfo() */
	if (mp->flags & NEED_FD)
		will_return(__wrap_udev_device_get_devnode, _mocked_filename);

	/* scsi_ioctl_pathinfo() */
	if (mask & DI_SERIAL) {
		will_return(__wrap_udev_device_get_subsystem, "scsi");
		will_return(__wrap_udev_device_get_sysname, hbtl);
	}

	if (mask & DI_WWID) {
		/* get_udev_uid() */
		will_return(__wrap_udev_device_get_property_value,
			    mp->wwid);
	}

	if (mask & DI_PRIO && mp->flags & NEED_SELECT_PRIO) {

		/* sysfs_get_timeout, again (!?) */
		will_return(__wrap_udev_device_get_subsystem, "scsi");
		will_return(__wrap_udev_device_get_sysattr_value, "180");

	}
}

void mock_store_pathinfo(int mask,  const struct mocked_path *mp)
{
	will_return(__wrap_udev_device_get_sysname, mp->devnode);
	mock_pathinfo(mask, mp);
}

struct path *mock_path__(vector pathvec,
			 const char *vnd, const char *prd,
			 const char *rev, const char *wwid,
			 const char *dev,
			 unsigned int flags, int mask)
{
	struct mocked_path mop;
	struct path *pp;
	struct config *conf;
	int r;

	fill_mocked_path(&mop, vnd, prd, rev, wwid, dev, flags);
	mock_store_pathinfo(mask, &mop);

	conf = get_multipath_config();
	r = store_pathinfo(pathvec, conf, (void *)&mop, mask, &pp);
	put_multipath_config(conf);

	if (flags & BL_MASK) {
		assert_int_equal(r, PATHINFO_SKIPPED);
		return NULL;
	}
	assert_int_equal(r, PATHINFO_OK);
	assert_non_null(pp);
	return pp;
}


struct multipath *mock_multipath__(struct vectors *vecs, struct path *pp)
{
	struct multipath *mp;
	struct config *conf;
	struct mocked_path mop;
	/* pretend new dm, use minio_rq,  */
	static const unsigned int fake_dm_tgt_version[3] = { 1, 1, 1 };

	mocked_path_from_path(&mop, pp);
	/* pathinfo() call in adopt_paths */
	mock_pathinfo(DI_CHECKER|DI_PRIO, &mop);

	mp = add_map_with_path(vecs, pp, 1, NULL);
	assert_ptr_not_equal(mp, NULL);

	/* TBD: mock setup_map() ... */
	conf = get_multipath_config();
	select_pgpolicy(conf, mp);
	select_no_path_retry(conf, mp);
	will_return(__wrap_libmp_get_version, fake_dm_tgt_version);
	select_retain_hwhandler(conf, mp);
	will_return(__wrap_libmp_get_version, fake_dm_tgt_version);
	select_minio(conf, mp);
	put_multipath_config(conf);

	return mp;
}
