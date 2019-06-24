/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2019 Martin Wilck, SUSE Linux GmbH, Nuremberg */

#define _GNU_SOURCE
#include <sys/types.h>
#include <regex.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <scsi/sg.h>
#include "unaligned.h"
#include "debug.h"
#include "vector.h"
#include "structs.h"
#include "discovery.h"
#include "globals.c"

#define VPD_BUFSIZ 4096

struct vpdtest {
	unsigned char vpdbuf[VPD_BUFSIZ];
	char wwid[WWID_SIZE];
};

static int setup(void **state)
{
	struct vpdtest *vt = malloc(sizeof(*vt));

	if (vt == NULL)
		return -1;
	*state = vt;
	return 0;
}

static int teardown(void **state)
{
	struct vpdtest *vt = *state;

	free(vt);
	*state = NULL;
	return 0;
}

/* vendor_id should have less than 8 chars to test space handling */
static const char vendor_id[] = "Linux";
static const char test_id[] =
	"A123456789AbcDefB123456789AbcDefC123456789AbcDefD123456789AbcDef";

int __wrap_ioctl(int fd, unsigned long request, void *param)
{
	int len;
	struct sg_io_hdr *io_hdr;
	unsigned char *val;

	len = mock();
	io_hdr = (struct sg_io_hdr *)param;
	assert_in_range(len, 0, io_hdr->dxfer_len);
	val = mock_ptr_type(unsigned char *);
	io_hdr->status = 0;
	memcpy(io_hdr->dxferp, val, len);
	return 0;
}


/**
 * create_vpd80() - create a "unit serial number" VPD page.
 * @buf:	VPD buffer
 * @bufsiz:	length of VPD buffer
 * @id:		input ID
 * @size:	value for the "page length" field
 * @len:	actual number of characters to use from @id
 *
 * If len < size, the content will be right aligned, as mandated by the
 * SPC spec.
 *
 * Return:	VPD length.
 */
static int create_vpd80(unsigned char *buf, size_t bufsiz, const char *id,
			int size, int len)
{
	assert_true(len <= size);

	memset(buf, 0, bufsiz);
	buf[1] = 0x80;
	put_unaligned_be16(size, buf + 2);

	memset(buf + 4, ' ', size - len);
	memcpy(buf + 4 + size - len, id, len);
	return size + 4;
}

static int _hex2bin(const char hx)
{
	assert_true(isxdigit(hx));
	if (hx >= '0' && hx <= '9')
		return hx - '0';
	if (hx >= 'a' && hx <= 'f')
		return hx - 'a' + 10;
	if (hx >= 'A' && hx <= 'F')
		return hx - 'A' + 10;
	return -1;
}

static void hex2bin(unsigned char *dst, const char *src,
		    size_t dstlen, size_t srclen)
{
	const char *sc;
	unsigned char *ds;

	assert(srclen % 2 == 0);
	for (sc = src, ds = dst;
	     sc < src + srclen &&  ds < dst + dstlen;
	     sc += 2, ++ds)
		*ds = 16 * _hex2bin(sc[0]) + _hex2bin(sc[1]);
}

/**
 * create_t10_vendor_id_desc() - Create T10 vendor ID
 * @desc:	descriptor buffer
 * @id:		input ID
 * @n:		number of characters to use for ID
 *		(includes 8 bytes for vendor ID!)
 *
 * Create a "T10 vendor specific ID" designation descriptor.
 * The vendor field (8 bytes) is filled with vendor_id (above).
 *
 * Return:	descriptor length.
 */
static int create_t10_vendor_id_desc(unsigned char *desc,
				     const char *id, size_t n)
{
	int vnd_len = sizeof(vendor_id) - 1;

	/* code set: ascii */
	desc[0] = 2;
	/* type: 10 vendor ID */
	desc[1] = 1;
	desc[2] = 0;
	desc[3] = n;

	memcpy(desc + 4, (const unsigned char *)vendor_id, vnd_len);
	memset(desc + 4 + vnd_len, ' ', 8 - vnd_len);
	memcpy(desc + 4 + 8, (const unsigned char *)id, n - 8);

	return n + 4;
}

/**
 * create_eui64_desc() - create EUI64 descriptor.
 * @desc, @id:	see above.
 * @n:		number of bytes (8, 12, or 16).
 *
 * Create an EUI64 designation descriptor.
 *
 * Return:	descriptor length.
 */
static int create_eui64_desc(unsigned char *desc,
				 const char *id, size_t n)
{
	assert_true(n == 8 || n == 12 || n == 16);

	/* code set: binary */
	desc[0] = 1;
	/* type: EUI64 */
	desc[1] = 2;
	desc[2] = 0;
	desc[3] = n;

	hex2bin(desc + 4, id, n, 2 * n);
	return n + 4;
}

/**
 * create_naa_desc() - create an NAA designation descriptor
 * @desc, @id:	see above.
 * @naa:	Name Address Authority field (2, 3, 5, or 6).
 *
 * Return:	descriptor length.
 */
static int create_naa_desc(unsigned char *desc,
			       const char *id, int naa)
{
	assert_true(naa == 2 || naa == 3 || naa == 5 || naa == 6);

	/* code set: binary */
	desc[0] = 1;
	/* type: NAA */
	desc[1] = 3;
	desc[2] = 0;
	desc[4] = _hex2bin(id[0]) | (naa << 4);
	switch (naa) {
	case 2:
	case 3:
	case 5:
		hex2bin(desc + 5, id + 1, 7, 14);
		desc[3] = 8;
		return 12;
	case 6:
		hex2bin(desc + 5, id + 1, 15, 30);
		desc[3] = 16;
		return 20;
	default:
		return 0;
	}
}

/* type and flags for SCSI name string designation descriptor */
enum {
	STR_EUI = 0,
	STR_NAA,
	STR_IQN,
	STR_MASK = 0xf,
	ZERO_LAST = 0x10,  /* flag to zero out some bytes at string end */
};

static const char * const str_prefix[] = {
	[STR_EUI] = "eui.",
	[STR_NAA] = "naa.",
	[STR_IQN] = "iqn.",
};

static const char byte0[] = {
	[STR_EUI] = '2',
	[STR_NAA] = '3',
	[STR_IQN] = '8',
};

/**
 * create_scsi_string_desc() - create a SCSI name string descriptor.
 * @desc, @id:	see above.
 * @typ:	one of STR_EUI, STR_NAA, STR_IQN, possibly ORd with ZERO_LAST
 * @maxlen:	number of characters to use from input ID.
 *
 * If ZERO_LAST is set, zero out the last byte.
 *
 * Return:	descriptor length.
 */
static int create_scsi_string_desc(unsigned char *desc,
				   const char *id, int typ, int maxlen)
{
	int len, plen;
	int type = typ & STR_MASK;

	/* code set: UTF-8 */
	desc[0] = 3;
	/* type: SCSI string */
	desc[1] = 8;
	desc[2] = 0;

	assert_in_range(type, STR_EUI, STR_IQN);
	assert_true(maxlen % 4 == 0);
	len = snprintf((char *)(desc + 4), maxlen, "%s%s",
		       str_prefix[type], id);
	if (len > maxlen)
		len = maxlen;
	/* zero-pad */
	if (typ & ZERO_LAST)
		len -= 2;
	plen = 4 * ((len - 1) / 4) + 4;
	memset(desc + 4 + len, '\0', plen - len);
	desc[3] = plen;
	return plen + 4;
}

/**
 * create_vpd83() - create "device identification" VPD page
 * @buf, @bufsiz, @id:	see above.
 * @type:	descriptor type to use (1, 2, 3, 8)
 * @parm:	opaque parameter (e.g. means "naa" for NAA type)
 * @len:	designator length (exact meaning depends on designator type)
 *
 * Create a "device identification" VPD page with a single
 * designation descriptor.
 *
 * Return:	VPD page length.
 */
static int create_vpd83(unsigned char *buf, size_t bufsiz, const char *id,
			uint8_t type, int parm, int len)
{
	unsigned char *desc;
	int n = 0;

	memset(buf, 0, bufsiz);
	buf[1] = 0x83;

	desc = buf + 4;
	switch (type) {
	case 1:
		n = create_t10_vendor_id_desc(desc, id, len);
		break;
	case 2:
		n = create_eui64_desc(desc, id, len);
		break;
	case 3:
		n = create_naa_desc(desc, id, parm);
		break;
	case 8:
		n = create_scsi_string_desc(desc, id, parm, len);
		break;
	default:
		break;
	}
	put_unaligned_be16(n, buf + 2);
	return n + 4;
}

/**
 * assert_correct_wwid() - test that a retrieved WWID matches expectations
 * @test:	test name
 * @expected:	expected WWID length
 * @returned:	WWID length as returned by code under test
 * @byte0, @byte1: leading chars that our code prepends to the ID
 *		(e.g. "36" for "NAA registered extended" type)
 * @lowercase:	set if lower case WWID is expected
 * @orig:	original ID string, may be longer than wwid
 * @wwid:	WWID as returned by code under test
 */
static void assert_correct_wwid(const char *test,
				int expected, int returned,
				int byte0, int byte1, bool lowercase,
				const char *orig,
				const char *wwid)
{
	int ofs = 0, i;

	condlog(2, "%s: exp/ret: %d/%d, wwid: %s", test,
		expected, returned, wwid);
	/*
	 * byte0 and byte1 are the leading chars that our code prepends
	 * to the ID to indicate the designation descriptor type, .
	 */
	if (byte0 != 0) {
		assert_int_equal(byte0, wwid[0]);
		++ofs;
	}
	if (byte1 != 0) {
		assert_int_equal(byte1, wwid[1]);
		++ofs;
	}
	/* check matching length, and length of WWID string */
	assert_int_equal(expected, returned);
	assert_int_equal(returned, strlen(wwid));
	/* check expected string value up to expected length */
	for (i = 0; i < returned - ofs; i++)
		assert_int_equal(wwid[ofs + i],
				 lowercase ? tolower(orig[i]) : orig[i]);
}

/*
 * For T10 vendor ID - replace sequences of spaces with a single underscore.
 * Use a different implementation then libmultipath, deliberately.
 */
static char *subst_spaces(const char *src)
{
	char *dst = calloc(1, strlen(src) + 1);
	char *p;
	static regex_t *re;
	regmatch_t match;
	int rc;

	assert_non_null(dst);
	if (re == NULL) {
		re = calloc(1, sizeof(*re));
		assert_non_null(re);
		rc = regcomp(re, " +", REG_EXTENDED);
		assert_int_equal(rc, 0);
	}

	for (rc = regexec(re, src, 1, &match, 0), p = dst;
	    rc == 0;
	    src += match.rm_eo, rc = regexec(re, src, 1, &match, 0)) {
		memcpy(p, src, match.rm_so);
		p += match.rm_so;
		*p = '_';
		++p;
	}
	assert_int_equal(rc, REG_NOMATCH);
	strcpy(p, src);
	return dst;
}

/**
 * test_vpd_vnd_LEN_WLEN() - test code for VPD 83, T10 vendor ID
 * @LEN:	ID length in the VPD page (includes 8 byte vendor ID)
 * @WLEN:	WWID buffer size
 *
 * The input ID is modified by inserting some spaces, to be able to
 * test the handling of spaces by the code. This is relevant only for
 * a minimum input length of 24.
 * The expected result must be adjusted accordingly.
 */
#define make_test_vpd_vnd(len, wlen)					\
static void test_vpd_vnd_ ## len ## _ ## wlen(void **state)             \
{                                                                       \
	struct vpdtest *vt = *state;					\
	int n, ret, rc;							\
	int exp_len;							\
	char *exp_wwid, *exp_subst, *input;				\
									\
	input = strdup(test_id);					\
	/* 8 vendor bytes collapsed to actual vendor ID length + 1 */	\
	/* and one '1' prepended */					\
	exp_len = len - 8 + sizeof(vendor_id) + 1;			\
									\
	/* insert some spaces to test space collapsing */		\
	input[15] = input[17] = input[18] = ' ';			\
	/* adjust expectation for space treatment */			\
	/* drop char for 2nd space on offset 17/18 */			\
	if (len >= 18 + 9)						\
		--exp_len;						\
	/* drop trailing single '_' if input ends with space */		\
	if (len == 15 + 9 || len == 17 + 9 || len == 18 + 9)		\
		--exp_len;						\
	if (exp_len >= wlen)						\
		exp_len = wlen - 1;					\
	n = create_vpd83(vt->vpdbuf, sizeof(vt->vpdbuf), input,		\
			 1, 0, len);					\
	rc = asprintf(&exp_wwid, "%s_%s", vendor_id, input);		\
	assert_int_not_equal(rc, -1);					\
	free(input);							\
	/* Replace spaces, like code under test */			\
	exp_subst = subst_spaces(exp_wwid);				\
	free(exp_wwid);							\
	will_return(__wrap_ioctl, n);					\
	will_return(__wrap_ioctl, vt->vpdbuf);				\
	ret = get_vpd_sgio(10, 0x83, vt->wwid, wlen);			\
	assert_correct_wwid("test_vpd_vnd_" #len "_" #wlen,		\
			    exp_len, ret, '1', 0, false,		\
			    exp_subst, vt->wwid);			\
	free(exp_subst);						\
}

/**
 * test_vpd_str_TYP_LEN_WLEN() - test code for VPD 83, SCSI name string
 * @TYP:	numeric value of STR_EUI, STR_NAA, STR_IQN above
 * @LEN:	ID length the VPD page
 * @WLEN:	WWID buffer size
 */
#define make_test_vpd_str(typ, len, wlen)				\
static void test_vpd_str_ ## typ ## _ ## len ## _ ## wlen(void **state) \
{                                                                       \
	struct vpdtest *vt = *state;					\
	int n, ret;							\
	int exp_len;							\
	int type = typ & STR_MASK;					\
									\
	n = create_vpd83(vt->vpdbuf, sizeof(vt->vpdbuf), test_id,	\
			 8, typ, len);					\
	exp_len = len - strlen(str_prefix[type]);			\
	if (typ & ZERO_LAST)						\
		exp_len--;						\
	if (exp_len >= wlen)						\
		exp_len = wlen - 1;					\
	will_return(__wrap_ioctl, n);					\
	will_return(__wrap_ioctl, vt->vpdbuf);				\
	ret = get_vpd_sgio(10, 0x83, vt->wwid, wlen);			\
	assert_correct_wwid("test_vpd_str_" #typ "_" #len "_" #wlen,	\
			    exp_len, ret, byte0[type], 0,		\
			    type != STR_IQN,				\
			    test_id, vt->wwid);				\
}

/**
 * test_vpd_naa_NAA_WLEN() - test code for VPD 83 NAA designation
 * @NAA:	Network Name Authority (2, 3, 5, or 6)
 * @WLEN:	WWID buffer size
 */
#define make_test_vpd_naa(naa, wlen)					\
static void test_vpd_naa_ ## naa ## _ ## wlen(void **state)             \
{                                                                       \
	struct vpdtest *vt = *state;					\
	int n, ret;							\
	int len, exp_len;						\
									\
	switch (naa) {							\
	case 2:								\
	case 3:								\
	case 5:								\
		len = 17;						\
		break;							\
	case 6:								\
		len = 33;						\
		break;							\
	}								\
	/* returned size is always uneven */                            \
	exp_len = wlen > len ? len :					\
		wlen % 2 == 0 ? wlen - 1 : wlen - 2;			\
									\
	n = create_vpd83(vt->vpdbuf, sizeof(vt->vpdbuf), test_id,	\
			 3, naa, 0);					\
	will_return(__wrap_ioctl, n);					\
	will_return(__wrap_ioctl, vt->vpdbuf);				\
	ret = get_vpd_sgio(10, 0x83, vt->wwid, wlen);			\
	assert_correct_wwid("test_vpd_naa_" #naa "_" #wlen,		\
			    exp_len, ret, '3', '0' + naa, true,		\
			    test_id, vt->wwid);				\
}

/**
 * test_vpd_eui_LEN_WLEN() - test code for VPD 83, EUI64
 * @LEN:	EUI64 length (8, 12, or 16)
 * @WLEN:	WWID buffer size
 */
#define make_test_vpd_eui(len, wlen)					\
static void test_vpd_eui_ ## len ## _ ## wlen(void **state)             \
{									\
	struct vpdtest *vt = *state;                                    \
	int n, ret;							\
	/* returned size is always uneven */				\
	int exp_len = wlen > 2 * len + 1 ? 2 * len + 1 :		\
		wlen % 2 == 0 ? wlen - 1 : wlen - 2;			\
									\
	n = create_vpd83(vt->vpdbuf, sizeof(vt->vpdbuf), test_id,	\
			 2, 0, len);					\
	will_return(__wrap_ioctl, n);					\
	will_return(__wrap_ioctl, vt->vpdbuf);				\
	ret = get_vpd_sgio(10, 0x83, vt->wwid, wlen);			\
	assert_correct_wwid("test_vpd_eui_" #len "_" #wlen,		\
			    exp_len, ret, '2', 0, true,			\
			    test_id, vt->wwid);				\
}

/**
 * test_vpd80_SIZE_LEN_WLEN() - test code for VPD 80
 * @SIZE, @LEN:	see create_vpd80()
 * @WLEN:	WWID buffer size
 */
#define make_test_vpd80(size, len, wlen)				\
static void test_vpd80_ ## size ## _ ## len ## _ ## wlen(void **state)  \
{									\
	struct vpdtest *vt = *state;                                    \
	int n, ret;							\
	int exp_len = len > 20 ? 20 : len;				\
	char *input = strdup(test_id);					\
									\
	/* insert trailing whitespace after pos 20 */			\
	memset(input + 20, ' ', sizeof(test_id) - 20);			\
	if (exp_len >= wlen)						\
		exp_len = wlen - 1;					\
	n = create_vpd80(vt->vpdbuf, sizeof(vt->vpdbuf), input,		\
			 size, len);					\
	will_return(__wrap_ioctl, n);					\
	will_return(__wrap_ioctl, vt->vpdbuf);				\
	ret = get_vpd_sgio(10, 0x80, vt->wwid, wlen);			\
	assert_correct_wwid("test_vpd80_" #size "_" #len "_" #wlen,	\
			    exp_len, ret, 0, 0, false,			\
			    input, vt->wwid);				\
	free(input);							\
}

/* VPD 80 */
/* Tests without trailing whitespace: 21 WWID bytes required */
make_test_vpd80(20, 20, 30);
make_test_vpd80(20, 20, 21);
make_test_vpd80(20, 20, 20);
make_test_vpd80(20, 20, 10);

/* Tests with 4 byte trailing whitespace: 21 WWID bytes required */
make_test_vpd80(24, 24, 30);
make_test_vpd80(24, 24, 25);
make_test_vpd80(24, 24, 24);
make_test_vpd80(24, 24, 21);
make_test_vpd80(24, 24, 20);

/* Tests with 4 byte leading whitespace: 17 WWID bytes required */
make_test_vpd80(20, 16, 30);
make_test_vpd80(20, 16, 17);
make_test_vpd80(20, 16, 16);

/* Tests with 4 byte leading whitespace: 21 WWID bytes required */
make_test_vpd80(24, 20, 21);
make_test_vpd80(24, 20, 20);

/* Tests with leading and trailing whitespace: 21 WWID bytes required */
make_test_vpd80(30, 24, 30);
make_test_vpd80(30, 24, 21);
make_test_vpd80(30, 24, 20);

/* VPD 83, T10 vendor ID */
make_test_vpd_vnd(40, 40);
make_test_vpd_vnd(40, 30);
make_test_vpd_vnd(30, 20);
make_test_vpd_vnd(29, 30);
make_test_vpd_vnd(28, 30);
make_test_vpd_vnd(27, 30); /* space at end */
make_test_vpd_vnd(26, 30); /* space at end */
make_test_vpd_vnd(25, 30);
make_test_vpd_vnd(24, 30); /* space at end */
make_test_vpd_vnd(23, 30);
make_test_vpd_vnd(24, 20);
make_test_vpd_vnd(23, 20);
make_test_vpd_vnd(22, 20);
make_test_vpd_vnd(21, 20);
make_test_vpd_vnd(20, 20);
make_test_vpd_vnd(19, 20);
make_test_vpd_vnd(20, 10);
make_test_vpd_vnd(10, 10);

/* EUI64 tests */
/* 64bit, WWID size: 18 */
make_test_vpd_eui(8, 32);
make_test_vpd_eui(8, 18);
make_test_vpd_eui(8, 17);
make_test_vpd_eui(8, 16);
make_test_vpd_eui(8, 10);

/* 96 bit, WWID size: 26 */
make_test_vpd_eui(12, 32);
make_test_vpd_eui(12, 26);
make_test_vpd_eui(12, 25);
make_test_vpd_eui(12, 20);
make_test_vpd_eui(12, 10);

/* 128 bit, WWID size: 34 */
make_test_vpd_eui(16, 40);
make_test_vpd_eui(16, 34);
make_test_vpd_eui(16, 33);
make_test_vpd_eui(16, 20);

/* NAA IEEE registered extended (36), WWID size: 34 */
make_test_vpd_naa(6, 40);
make_test_vpd_naa(6, 34);
make_test_vpd_naa(6, 33);
make_test_vpd_naa(6, 32);
make_test_vpd_naa(6, 20);

/* NAA IEEE registered (35), WWID size: 18 */
make_test_vpd_naa(5, 20);
make_test_vpd_naa(5, 18);
make_test_vpd_naa(5, 17);
make_test_vpd_naa(5, 16);

/* NAA local (33), WWID size: 18 */
make_test_vpd_naa(3, 20);
make_test_vpd_naa(3, 18);
make_test_vpd_naa(3, 17);
make_test_vpd_naa(3, 16);

/* NAA IEEE extended (32), WWID size: 18 */
make_test_vpd_naa(2, 20);
make_test_vpd_naa(2, 18);
make_test_vpd_naa(2, 17);
make_test_vpd_naa(2, 16);

/* SCSI Name string: EUI64, WWID size: 17 */
make_test_vpd_str(0, 20, 18)
make_test_vpd_str(0, 20, 17)
make_test_vpd_str(0, 20, 16)
make_test_vpd_str(0, 20, 15)

/* SCSI Name string: EUI64, zero padded, WWID size: 16 */
make_test_vpd_str(16, 20, 18)
make_test_vpd_str(16, 20, 17)
make_test_vpd_str(16, 20, 16)
make_test_vpd_str(16, 20, 15)

/* SCSI Name string: NAA, WWID size: 17 */
make_test_vpd_str(1, 20, 18)
make_test_vpd_str(1, 20, 17)
make_test_vpd_str(1, 20, 16)
make_test_vpd_str(1, 20, 15)

/* SCSI Name string: NAA, zero padded, WWID size: 16 */
make_test_vpd_str(17, 20, 18)
make_test_vpd_str(17, 20, 17)
make_test_vpd_str(17, 20, 16)
make_test_vpd_str(17, 20, 15)

/* SCSI Name string: IQN, WWID size: 17 */
make_test_vpd_str(2, 20, 18)
make_test_vpd_str(2, 20, 17)
make_test_vpd_str(2, 20, 16)
make_test_vpd_str(2, 20, 15)

/* SCSI Name string: IQN, zero padded, WWID size: 16 */
make_test_vpd_str(18, 20, 18)
make_test_vpd_str(18, 20, 17)
make_test_vpd_str(18, 20, 16)
make_test_vpd_str(18, 20, 15)

static int test_vpd(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_vpd80_20_20_30),
		cmocka_unit_test(test_vpd80_20_20_21),
		cmocka_unit_test(test_vpd80_20_20_20),
		cmocka_unit_test(test_vpd80_20_20_10),
		cmocka_unit_test(test_vpd80_24_24_30),
		cmocka_unit_test(test_vpd80_24_24_25),
		cmocka_unit_test(test_vpd80_24_24_24),
		cmocka_unit_test(test_vpd80_24_24_21),
		cmocka_unit_test(test_vpd80_24_24_20),
		cmocka_unit_test(test_vpd80_20_16_30),
		cmocka_unit_test(test_vpd80_20_16_17),
		cmocka_unit_test(test_vpd80_20_16_16),
		cmocka_unit_test(test_vpd80_24_20_21),
		cmocka_unit_test(test_vpd80_24_20_20),
		cmocka_unit_test(test_vpd80_30_24_30),
		cmocka_unit_test(test_vpd80_30_24_21),
		cmocka_unit_test(test_vpd80_30_24_20),
		cmocka_unit_test(test_vpd_vnd_40_40),
		cmocka_unit_test(test_vpd_vnd_40_30),
		cmocka_unit_test(test_vpd_vnd_30_20),
		cmocka_unit_test(test_vpd_vnd_29_30),
		cmocka_unit_test(test_vpd_vnd_28_30),
		cmocka_unit_test(test_vpd_vnd_27_30),
		cmocka_unit_test(test_vpd_vnd_26_30),
		cmocka_unit_test(test_vpd_vnd_25_30),
		cmocka_unit_test(test_vpd_vnd_24_30),
		cmocka_unit_test(test_vpd_vnd_23_30),
		cmocka_unit_test(test_vpd_vnd_24_20),
		cmocka_unit_test(test_vpd_vnd_23_20),
		cmocka_unit_test(test_vpd_vnd_22_20),
		cmocka_unit_test(test_vpd_vnd_21_20),
		cmocka_unit_test(test_vpd_vnd_20_20),
		cmocka_unit_test(test_vpd_vnd_19_20),
		cmocka_unit_test(test_vpd_vnd_20_10),
		cmocka_unit_test(test_vpd_vnd_10_10),
		cmocka_unit_test(test_vpd_eui_8_32),
		cmocka_unit_test(test_vpd_eui_8_18),
		cmocka_unit_test(test_vpd_eui_8_17),
		cmocka_unit_test(test_vpd_eui_8_16),
		cmocka_unit_test(test_vpd_eui_8_10),
		cmocka_unit_test(test_vpd_eui_12_32),
		cmocka_unit_test(test_vpd_eui_12_26),
		cmocka_unit_test(test_vpd_eui_12_25),
		cmocka_unit_test(test_vpd_eui_12_20),
		cmocka_unit_test(test_vpd_eui_12_10),
		cmocka_unit_test(test_vpd_eui_16_40),
		cmocka_unit_test(test_vpd_eui_16_34),
		cmocka_unit_test(test_vpd_eui_16_33),
		cmocka_unit_test(test_vpd_eui_16_20),
		cmocka_unit_test(test_vpd_naa_6_40),
		cmocka_unit_test(test_vpd_naa_6_34),
		cmocka_unit_test(test_vpd_naa_6_33),
		cmocka_unit_test(test_vpd_naa_6_32),
		cmocka_unit_test(test_vpd_naa_6_20),
		cmocka_unit_test(test_vpd_naa_5_20),
		cmocka_unit_test(test_vpd_naa_5_18),
		cmocka_unit_test(test_vpd_naa_5_17),
		cmocka_unit_test(test_vpd_naa_5_16),
		cmocka_unit_test(test_vpd_naa_3_20),
		cmocka_unit_test(test_vpd_naa_3_18),
		cmocka_unit_test(test_vpd_naa_3_17),
		cmocka_unit_test(test_vpd_naa_3_16),
		cmocka_unit_test(test_vpd_naa_2_20),
		cmocka_unit_test(test_vpd_naa_2_18),
		cmocka_unit_test(test_vpd_naa_2_17),
		cmocka_unit_test(test_vpd_naa_2_16),
		cmocka_unit_test(test_vpd_str_0_20_18),
		cmocka_unit_test(test_vpd_str_0_20_17),
		cmocka_unit_test(test_vpd_str_0_20_16),
		cmocka_unit_test(test_vpd_str_0_20_15),
		cmocka_unit_test(test_vpd_str_16_20_18),
		cmocka_unit_test(test_vpd_str_16_20_17),
		cmocka_unit_test(test_vpd_str_16_20_16),
		cmocka_unit_test(test_vpd_str_16_20_15),
		cmocka_unit_test(test_vpd_str_1_20_18),
		cmocka_unit_test(test_vpd_str_1_20_17),
		cmocka_unit_test(test_vpd_str_1_20_16),
		cmocka_unit_test(test_vpd_str_1_20_15),
		cmocka_unit_test(test_vpd_str_17_20_18),
		cmocka_unit_test(test_vpd_str_17_20_17),
		cmocka_unit_test(test_vpd_str_17_20_16),
		cmocka_unit_test(test_vpd_str_17_20_15),
		cmocka_unit_test(test_vpd_str_2_20_18),
		cmocka_unit_test(test_vpd_str_2_20_17),
		cmocka_unit_test(test_vpd_str_2_20_16),
		cmocka_unit_test(test_vpd_str_2_20_15),
		cmocka_unit_test(test_vpd_str_18_20_18),
		cmocka_unit_test(test_vpd_str_18_20_17),
		cmocka_unit_test(test_vpd_str_18_20_16),
		cmocka_unit_test(test_vpd_str_18_20_15),
	};
	return cmocka_run_group_tests(tests, setup, teardown);
}

int main(void)
{
	int ret = 0;

	ret += test_vpd();
	return ret;
}
