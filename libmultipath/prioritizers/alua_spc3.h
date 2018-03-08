/*
 * (C) Copyright IBM Corp. 2004, 2005   All Rights Reserved.
 *
 * spc3.h
 *
 * Tool to make use of a SCSI-feature called Asymmetric Logical Unit Access.
 * It determines the ALUA state of a device and prints a priority value to
 * stdout.
 *
 * Author(s): Jan Kunigk
 *            S. Bader <shbader@de.ibm.com>
 *
 * This file is released under the GPL.
 */
#ifndef __SPC3_H__
#define __SPC3_H__

/*=============================================================================
 * Definitions to support the standard inquiry command as defined in SPC-3.
 * If the evpd (enable vital product data) bit is set the data that will be
 * returned is selected by the page field. This field must be 0 if the evpd
 * bit is not set.
 *=============================================================================
 */
#define OPERATION_CODE_INQUIRY		0x12

struct inquiry_command {
	unsigned char	op;
	unsigned char	b1;		/* xxxxxx.. = reserved               */
					/* ......x. = obsolete               */
					/* .......x = evpd                   */
	unsigned char	page;
	unsigned char	length[2];
	unsigned char	control;
} __attribute__((packed));

static inline void
inquiry_command_set_evpd(struct inquiry_command *ic)
{
	ic->b1 |= 1;
}

/*-----------------------------------------------------------------------------
 * Data returned by the standard inquiry command.
 *-----------------------------------------------------------------------------
 *
 * Peripheral qualifier codes.
 */
#define PQ_CONNECTED					0x0
#define PQ_DISCONNECTED					0x1
#define PQ_UNSUPPORTED					0x3

/* Defined peripheral device types. */
#define PDT_DIRECT_ACCESS				0x00
#define PDT_SEQUENTIAL_ACCESS				0x01
#define PDT_PRINTER					0x02
#define PDT_PROCESSOR					0x03
#define PDT_WRITE_ONCE					0x04
#define PDT_CD_DVD					0x05
#define PDT_SCANNER					0x06
#define PDT_OPTICAL_MEMORY				0x07
#define PDT_MEDIUM_CHANGER				0x08
#define PDT_COMMUNICATIONS				0x09
#define PDT_STORAGE_ARRAY_CONTROLLER			0x0c
#define PDT_ENCLOSURE_SERVICES				0x0d
#define PDT_SIMPLIFIED_DIRECT_ACCESS			0x0e
#define PDT_OPTICAL_CARD_READER_WRITER			0x0f
#define PDT_BRIDGE_CONTROLLER				0x10
#define PDT_OBJECT_BASED				0x11
#define PDT_AUTOMATION_INTERFACE			0x12
#define PDT_LUN						0x1e
#define PDT_UNKNOWN					0x1f

/* Defined version codes. */
#define VERSION_NONE					0x00
#define VERSION_SPC					0x03
#define VERSION_SPC2					0x04
#define VERSION_SPC3					0x05

/* Defined TPGS field values. */
#define TPGS_UNDEF					 -1
#define TPGS_NONE					0x0
#define TPGS_IMPLICIT					0x1
#define TPGS_EXPLICIT					0x2
#define TPGS_BOTH					0x3

struct inquiry_data {
	unsigned char	b0;		/* xxx..... = peripheral_qualifier   */
					/* ...xxxxx = peripheral_device_type */
	unsigned char	b1;             /* x....... = removable medium       */
					/* .xxxxxxx = reserved              */
	unsigned char	version;
	unsigned char	b3;		/* xx...... = obsolete               */
					/* ..x..... = normal aca supported   */
					/* ...x.... = hirarchichal lun supp. */
					/* ....xxxx = response format        */
					/*            2 is spc-3 format      */
	unsigned char	length;
	unsigned char	b5;		/* x....... = storage controller     */
					/*            component supported    */
					/* .x...... = access controls coord. */
					/* ..xx.... = target port group supp.*/
					/* ....x... = third party copy supp. */
					/* .....xx. = reserved               */
					/* .......x = protection info supp.  */
	unsigned char	b6;		/* x....... = bque                   */
					/* .x...... = enclosure services sup.*/
					/* ..x..... = vs1                    */
					/* ...x.... = multiport support      */
					/* ....x... = medium changer         */
					/* .....xx. = obsolete               */
					/* .......x = add16                  */
	unsigned char	b7;		/* xx...... = obsolete               */
					/* ..x..... = wbus16                 */
					/* ...x.... = sync                   */
					/* ....x... = linked commands supp.  */
					/* .....x.. = obsolete               */
					/* ......x. = command queue support  */
					/* .......x = vs2                    */
	unsigned char	vendor_identification[8];
	unsigned char	product_identification[16];
	unsigned char	product_revision[4];
	unsigned char	vendor_specific[20];
	unsigned char	b56;		/* xxxx.... = reserved               */
					/* ....xx.. = clocking               */
					/* ......x. = qas                    */
					/* .......x = ius                    */
	unsigned char	reserved4;
	unsigned char	version_descriptor[8][2];
	unsigned char	reserved5[22];
	unsigned char	vendor_parameters[0];
} __attribute__((packed));

static inline int
inquiry_data_get_tpgs(struct inquiry_data *id)
{
	return (id->b5 >> 4) & 3;
}

/*-----------------------------------------------------------------------------
 * Inquiry data returned when requesting vital product data page 0x83.
 *-----------------------------------------------------------------------------
 */
#define CODESET_BINARY			0x1
#define CODESET_ACSII			0x2
#define CODESET_UTF8			0x3

#define ASSOCIATION_UNIT		0x0
#define ASSOCIATION_PORT		0x1
#define ASSOCIATION_DEVICE		0x2

#define IDTYPE_VENDOR_SPECIFIC		0x0
#define IDTYPE_T10_VENDOR_ID		0x1
#define IDTYPE_EUI64			0x2
#define IDTYPE_NAA			0x3
#define IDTYPE_RELATIVE_TPG_ID		0x4
#define IDTYPE_TARGET_PORT_GROUP	0x5
#define IDTYPE_LUN_GROUP		0x6
#define IDTYPE_MD5_LUN_ID		0x7
#define IDTYPE_SCSI_NAME_STRING		0x8

struct vpd83_tpg_dscr {
	unsigned char		reserved1[2];
	unsigned char		tpg[2];
} __attribute__((packed));

struct vpd83_dscr {
	unsigned char		b0;	/* xxxx.... = protocol id            */
					/* ....xxxx = codeset                */
	unsigned char		b1;	/* x....... = protocol id valid      */
					/* .x...... = reserved               */
					/* ..xx.... = association            */
					/* ....xxxx = id type                */
	unsigned char		reserved2;
	unsigned char		length;				/* size-4    */
	unsigned char		data[0];
} __attribute__((packed));

static inline int
vpd83_dscr_istype(struct vpd83_dscr *d, unsigned char type)
{
	return ((d->b1 & 7) == type);
}

struct vpd83_data {
	unsigned char		b0;	/* xxx..... = peripheral_qualifier   */
					/* ...xxxxx = peripheral_device_type */
	unsigned char		page_code;			/* 0x83      */
	unsigned char		length[2];			/* size-4    */
	struct vpd83_dscr	data[0];
} __attribute__((packed));

/*-----------------------------------------------------------------------------
 * This macro should be used to walk through all identification descriptors
 * defined in the code page 0x83.
 * The argument p is a pointer to the code page 0x83 data and d is used to
 * point to the current descriptor.
 *-----------------------------------------------------------------------------
 */
#define FOR_EACH_VPD83_DSCR(p, d) \
		for( \
			d = p->data; \
			(((char *) d) - ((char *) p)) < \
			get_unaligned_be16(p->length); \
			d = (struct vpd83_dscr *) \
				((char *) d + d->length + 4) \
		)

/*=============================================================================
 * The following structures and macros are used to call the report target port
 * groups command defined in SPC-3.
 * This command is used to get information about the target port groups (which
 * states are supported, which ports belong to this group, and so on) and the
 * current state of each target port group.
 *=============================================================================
 */
#define OPERATION_CODE_RTPG		0xa3
#define SERVICE_ACTION_RTPG		0x0a

struct rtpg_command {
	unsigned char		op;	/* 0xa3                              */
	unsigned char		b1;	/* xxx..... = reserved               */
					/* ...xxxxx = service action (0x0a)  */
	unsigned char			reserved2[4];
	unsigned char			length[4];
	unsigned char			reserved3;
	unsigned char			control;
} __attribute__((packed));

static inline void
rtpg_command_set_service_action(struct rtpg_command *cmd)
{
	cmd->b1 = (cmd->b1 & 0xe0) | SERVICE_ACTION_RTPG;
}

struct rtpg_tp_dscr {
	unsigned char			obsolete1[2];
	/* The Relative Target Port Identifier of a target port. */
	unsigned char			rtpi[2];
} __attribute__((packed));

#define AAS_OPTIMIZED			0x0
#define AAS_NON_OPTIMIZED		0x1
#define AAS_STANDBY			0x2
#define AAS_UNAVAILABLE			0x3
#define AAS_LBA_DEPENDENT		0x4
#define AAS_RESERVED			0x5
#define AAS_OFFLINE			0xe
#define AAS_TRANSITIONING		0xf

#define TPG_STATUS_NONE			0x0
#define TPG_STATUS_SET			0x1
#define TPG_STATUS_IMPLICIT_CHANGE	0x2

struct rtpg_tpg_dscr {
	unsigned char	b0;		/* x....... = pref(ered) port        */
					/* .xxx.... = reserved               */
					/* ....xxxx = asymetric access state */
	unsigned char	b1;		/* xxx..... = reserved               */
					/* ...x.... = LBA dependent support  */
					/* ....x... = unavailable support    */
					/* .....x.. = standby support        */
					/* ......x. = non-optimized support  */
					/* .......x = optimized support      */
	unsigned char			tpg[2];
	unsigned char			reserved3;
	unsigned char			status;
	unsigned char			vendor_unique;
	unsigned char			port_count;
	struct rtpg_tp_dscr		data[0];
} __attribute__((packed));

static inline int
rtpg_tpg_dscr_get_aas(struct rtpg_tpg_dscr *d)
{
	return (d->b0 & 0x8f);
}

struct rtpg_data {
	unsigned char			length[4];		/* size-4 */
	struct rtpg_tpg_dscr		data[0];
} __attribute__((packed));

#define RTPG_FOR_EACH_PORT_GROUP(p, g) \
		for( \
			g = &(p->data[0]); \
			(((char *) g) - ((char *) p)) < get_unaligned_be32(p->length); \
			g = (struct rtpg_tpg_dscr *) ( \
				((char *) g) + \
				sizeof(struct rtpg_tpg_dscr) + \
				g->port_count * sizeof(struct rtpg_tp_dscr) \
			) \
		)

#endif /* __SPC3_H__ */
