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
 * Some helper functions for getting and setting 16 and 32 bit values.
 *=============================================================================
 */
static inline unsigned short
get_uint16(unsigned char *p)
{
	return (p[0] << 8) + p[1];
}

static inline void
set_uint16(unsigned char *p, unsigned short v)
{
	p[0] = (v >> 8) & 0xff;
	p[1] = v & 0xff;
}

static inline unsigned int
get_uint32(unsigned char *p)
{
	return (p[0] << 24) + (p[1] << 16) + (p[2] << 8) + p[3];
}

static inline void
set_uint32(unsigned char *p, unsigned int v)
{
	p[0] = (v >> 24) & 0xff;
	p[1] = (v >> 16) & 0xff;
	p[2] = (v >>  8) & 0xff;
	p[3] = v & 0xff;
}

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
	unsigned char	reserved1			: 6;
	unsigned char	obsolete1			: 1;
	unsigned char	evpd				: 1;
	unsigned char	page;
	unsigned char	length[2];
	unsigned char	control;
} __attribute__((packed));

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
#define TPGS_NONE					0x0
#define TPGS_IMPLICIT					0x1
#define TPGS_EXPLICIT					0x2
#define TPGS_BOTH					0x3

struct inquiry_data {
	unsigned char	peripheral_qualifier		: 3;
	unsigned char	peripheral_device_type		: 5;
	/* Removable Medium Bit (1 == removable) */
	unsigned char	rmb				: 1;
	unsigned char	reserved1			: 7;
	unsigned char	version;
	unsigned char	obsolete1			: 2;
	/* Normal ACA Supported */
	unsigned char	norm_aca			: 1;
	/* Hierarchical LUN assignment support */
	unsigned char	hi_sup				: 1;
	/* If 2 then response data is as defined in SPC-3. */
	unsigned char	response_data_format		: 4;
	unsigned char	length;
	/* Storage Controller Component Supported. */
	unsigned char	sccs				: 1;
	/* Access Controls Cordinator. */
	unsigned char	acc				: 1;
	/* Target Port Group Support */
	unsigned char	tpgs				: 2;
	/* Third Party Copy support. */
	unsigned char	tpc				: 1;
	unsigned char	reserved2			: 2;
	/* PROTECTion information supported. */
	unsigned char	protect				: 1;
	/* Basic task management model supported (CmdQue must be 0). */
	unsigned char	bque				: 1;
	/* ENClosure SERVices supported. */
	unsigned char	encserv				: 1;
	unsigned char	vs1				: 1;
	/* MULTIPort support. */
	unsigned char	multip				: 1;
	/* Medium CHaNGeR. */
	unsigned char	mchngr				: 1;
	unsigned char	obsolete2			: 2;
	unsigned char	addr16				: 1;
	unsigned char	obsolete3			: 2;
	unsigned char	wbus16				: 1;
	unsigned char	sync				: 1;
	/* LINKed commands supported. */
	unsigned char	link				: 1;
	unsigned char	obsolete4			: 1;
	unsigned char	cmdque				: 1;
	unsigned char	vs2				: 1;
	unsigned char	vendor_identification[8];
	unsigned char	product_identification[8];
	unsigned char	product_revision[4];
	unsigned char	vendor_specific[20];
	unsigned char	reserved3			: 4;
	unsigned char	clocking			: 2;
	unsigned char	qas				: 1;
	unsigned char	ius				: 1;
	unsigned char	reserved4;
	unsigned char	version_descriptor[8][2];
	unsigned char	reserved5[22];
	unsigned char	vendor_parameters[0];
} __attribute__((packed));

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
	unsigned char		protocol_id			: 4;
	unsigned char		codeset				: 4;
	/* Set if the protocol_id field is valid. */
	unsigned char		piv				: 1;
	unsigned char		reserved1			: 1;
	unsigned char		association			: 2;
	unsigned char		id_type				: 4;
	unsigned char		reserved2;
	unsigned char		length;				/* size-4 */
	unsigned char		data[0];
} __attribute__((packed));

struct vpd83_data {
	unsigned char		peripheral_qualifier		: 3;
	unsigned char		peripheral_device_type		: 5;
	unsigned char		page_code;			/* 0x83 */
	unsigned char		length[2];			/* size-4 */
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
			get_uint16(p->length); \
			d = (struct vpd83_dscr *) \
				((char *) d + d->length + 4) \
		)

/*=============================================================================
 * The following stuctures and macros are used to call the report target port
 * groups command defined in SPC-3.
 * This command is used to get information about the target port groups (which
 * states are supported, which ports belong to this group, and so on) and the
 * current state of each target port group.
 *=============================================================================
 */
#define OPERATION_CODE_RTPG		0xa3
#define SERVICE_ACTION_RTPG		0x0a

struct rtpg_command {
	unsigned char			op;			/* 0xa3 */
	unsigned char			reserved1	: 3;
	unsigned char			service_action	: 5;	/* 0x0a */
	unsigned char			reserved2[4];
	unsigned char			length[4];
	unsigned char			reserved3;
	unsigned char			control;
} __attribute__((packed));

struct rtpg_tp_dscr {
	unsigned char			obsolete1[2];
	/* The Relative Target Port Identifier of a target port. */
	unsigned char			rtpi[2];
} __attribute__((packed));

#define AAS_OPTIMIZED			0x0
#define AAS_NON_OPTIMIZED		0x1
#define AAS_STANDBY			0x2
#define AAS_UNAVAILABLE			0x3
#define AAS_TRANSITIONING		0xf

#define TPG_STATUS_NONE			0x0
#define TPG_STATUS_SET			0x1
#define TPG_STATUS_IMPLICIT_CHANGE	0x2

struct rtpg_tpg_dscr {
	unsigned char			pref		: 1;
	unsigned char			reserved1	: 3;
	unsigned char			aas		: 4;
	unsigned char			reserved2	: 4;
	unsigned char			u_sup		: 1;
	unsigned char			s_sup		: 1;
	unsigned char			an_sup		: 1;
	unsigned char			ao_sup		: 1;
	unsigned char			tpg[2];
	unsigned char			reserved3;
	unsigned char			status;
	unsigned char			vendor_unique;
	unsigned char			port_count;
	struct rtpg_tp_dscr		data[0];
} __attribute__((packed));

struct rtpg_data {
	unsigned char			length[4];		/* size-4 */
	struct rtpg_tpg_dscr		data[0];
} __attribute__((packed));

#define RTPG_FOR_EACH_PORT_GROUP(p, g) \
		for( \
			g = &(p->data[0]); \
			(((char *) g) - ((char *) p)) < get_uint32(p->length); \
			g = (struct rtpg_tpg_dscr *) ( \
				((char *) g) + \
				sizeof(struct rtpg_tpg_dscr) + \
				g->port_count * sizeof(struct rtpg_tp_dscr) \
			) \
		)

#endif /* __SPC3_H__ */

