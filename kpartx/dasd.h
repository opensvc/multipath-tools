/*
 * dasd.h
 *
 * IBM DASD partition table handling.
 *
 * Mostly taken from drivers/s390/block/dasd.c
 *
 * Copyright (c) 2005, Hannes Reinecke, SUSE Linux Products GmbH
 * Copyright IBM Corporation, 2009
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _DASD_H
#define _DASD_H

typedef struct ttr
{
	uint16_t tt;
	uint8_t  r;
} __attribute__ ((packed)) ttr_t;

typedef struct cchhb
{
	uint16_t cc;
	uint16_t hh;
	uint8_t b;
} __attribute__ ((packed)) cchhb_t;

typedef struct cchh
{
	uint16_t cc;
	uint16_t hh;
} __attribute__ ((packed)) cchh_t;

typedef struct labeldate
{
	uint8_t  year;
	uint16_t day;
} __attribute__ ((packed)) labeldate_t;


typedef struct volume_label
{
	char volkey[4];         /* volume key = volume label                 */
	char vollbl[4];	        /* volume label                              */
	char volid[6];	        /* volume identifier                         */
	uint8_t security;	        /* security byte                             */
	cchhb_t vtoc;           /* VTOC address                              */
	char res1[5];	        /* reserved                                  */
	char cisize[4];	        /* CI-size for FBA,...                       */
				/* ...blanks for CKD                         */
	char blkperci[4];       /* no of blocks per CI (FBA), blanks for CKD */
	char labperci[4];       /* no of labels per CI (FBA), blanks for CKD */
	char res2[4];	        /* reserved                                  */
	char lvtoc[14];	        /* owner code for LVTOC                      */
	char res3[28];	        /* reserved                                  */
	uint8_t ldl_version;    /* version number, valid for ldl format      */
	uint64_t formatted_blocks; /* valid when ldl_version >= f2           */
} __attribute__ ((packed, aligned(__alignof__(int)))) volume_label_t;


typedef struct extent
{
	uint8_t  typeind;          /* extent type indicator                     */
	uint8_t  seqno;            /* extent sequence number                    */
	cchh_t llimit;          /* starting point of this extent             */
	cchh_t ulimit;          /* ending point of this extent               */
} __attribute__ ((packed)) extent_t;


typedef struct dev_const
{
	uint16_t DS4DSCYL;           /* number of logical cyls                  */
	uint16_t DS4DSTRK;           /* number of tracks in a logical cylinder  */
	uint16_t DS4DEVTK;           /* device track length                     */
	uint8_t  DS4DEVI;            /* non-last keyed record overhead          */
	uint8_t  DS4DEVL;            /* last keyed record overhead              */
	uint8_t  DS4DEVK;            /* non-keyed record overhead differential  */
	uint8_t  DS4DEVFG;           /* flag byte                               */
	uint16_t DS4DEVTL;           /* device tolerance                        */
	uint8_t  DS4DEVDT;           /* number of DSCB's per track              */
	uint8_t  DS4DEVDB;           /* number of directory blocks per track    */
} __attribute__ ((packed)) dev_const_t;


typedef struct format1_label
{
	char  DS1DSNAM[44];       /* data set name                           */
	uint8_t  DS1FMTID;           /* format identifier                       */
	char  DS1DSSN[6];         /* data set serial number                  */
	uint16_t DS1VOLSQ;           /* volume sequence number                  */
	labeldate_t DS1CREDT;     /* creation date: ydd                      */
	labeldate_t DS1EXPDT;     /* expiration date                         */
	uint8_t  DS1NOEPV;           /* number of extents on volume             */
	uint8_t  DS1NOBDB;           /* no. of bytes used in last direction blk */
	uint8_t  DS1FLAG1;           /* flag 1                                  */
	char  DS1SYSCD[13];       /* system code                             */
	labeldate_t DS1REFD;      /* date last referenced                    */
	uint8_t  DS1SMSFG;           /* system managed storage indicators       */
	uint8_t  DS1SCXTF;           /* sec. space extension flag byte          */
	uint16_t DS1SCXTV;           /* secondary space extension value         */
	uint8_t  DS1DSRG1;           /* data set organisation byte 1            */
	uint8_t  DS1DSRG2;           /* data set organisation byte 2            */
	uint8_t  DS1RECFM;           /* record format                           */
	uint8_t  DS1OPTCD;           /* option code                             */
	uint16_t DS1BLKL;            /* block length                            */
	uint16_t DS1LRECL;           /* record length                           */
	uint8_t  DS1KEYL;            /* key length                              */
	uint16_t DS1RKP;             /* relative key position                   */
	uint8_t  DS1DSIND;           /* data set indicators                     */
	uint8_t  DS1SCAL1;           /* secondary allocation flag byte          */
	char DS1SCAL3[3];         /* secondary allocation quantity           */
	ttr_t DS1LSTAR;           /* last used track and block on track      */
	uint16_t DS1TRBAL;           /* space remaining on last used track      */
	uint16_t res1;               /* reserved                                */
	extent_t DS1EXT1;         /* first extent description                */
	extent_t DS1EXT2;         /* second extent description               */
	extent_t DS1EXT3;         /* third extent description                */
	cchhb_t DS1PTRDS;         /* possible pointer to f2 or f3 DSCB       */
} __attribute__ ((packed)) format1_label_t;


/*
 * struct dasd_information_t
 * represents any data about the data, which is visible to userspace
 */
typedef struct dasd_information_t {
	unsigned int devno;		/* S/390 devno */
	unsigned int real_devno;	/* for aliases */
	unsigned int schid;		/* S/390 subchannel identifier */
	unsigned int cu_type  : 16;	/* from SenseID */
	unsigned int cu_model :  8;	/* from SenseID */
	unsigned int dev_type : 16;	/* from SenseID */
	unsigned int dev_model : 8;	/* from SenseID */
	unsigned int open_count;
	unsigned int req_queue_len;
	unsigned int chanq_len;		/* length of chanq */
	char type[4];			/* from discipline.name, 'none' for unknown */
	unsigned int status;		/* current device level */
	unsigned int label_block;	/* where to find the VOLSER */
	unsigned int FBA_layout;	/* fixed block size (like AIXVOL) */
	unsigned int characteristics_size;
	unsigned int confdata_size;
	char characteristics[64];	/* from read_device_characteristics */
	char configuration_data[256];	/* from read_configuration_data */
} dasd_information_t;

#define DASD_IOCTL_LETTER	 'D'
#define BIODASDINFO _IOR(DASD_IOCTL_LETTER,1,dasd_information_t)
#define BLKGETSIZE _IO(0x12,96)
#define BLKSSZGET _IO(0x12,104)
#define BLKGETSIZE64 _IOR(0x12,114,size_t) /* device size in bytes (u64 *arg)*/
#define LV_COMPAT_CYL 0xFFFE

/*
 * Only compile this on S/390. Doesn't make any sense
 * for other architectures.
 */

static unsigned char EBCtoASC[256] =
{
/* 0x00  NUL   SOH   STX   ETX  *SEL    HT  *RNL   DEL */
	0x00, 0x01, 0x02, 0x03, 0x07, 0x09, 0x07, 0x7F,
/* 0x08  -GE  -SPS  -RPT    VT    FF    CR    SO    SI */
	0x07, 0x07, 0x07, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
/* 0x10  DLE   DC1   DC2   DC3  -RES   -NL    BS  -POC
				-ENP  ->LF             */
	0x10, 0x11, 0x12, 0x13, 0x07, 0x0A, 0x08, 0x07,
/* 0x18  CAN    EM  -UBS  -CU1  -IFS  -IGS  -IRS  -ITB
						  -IUS */
	0x18, 0x19, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
/* 0x20  -DS  -SOS    FS  -WUS  -BYP    LF   ETB   ESC
				-INP                   */
	0x07, 0x07, 0x1C, 0x07, 0x07, 0x0A, 0x17, 0x1B,
/* 0x28  -SA  -SFE   -SM  -CSP  -MFA   ENQ   ACK   BEL
		     -SW                               */
	0x07, 0x07, 0x07, 0x07, 0x07, 0x05, 0x06, 0x07,
/* 0x30 ----  ----   SYN   -IR   -PP  -TRN  -NBS   EOT */
	0x07, 0x07, 0x16, 0x07, 0x07, 0x07, 0x07, 0x04,
/* 0x38 -SBS   -IT  -RFF  -CU3   DC4   NAK  ----   SUB */
	0x07, 0x07, 0x07, 0x07, 0x14, 0x15, 0x07, 0x1A,
/* 0x40   SP   RSP           ä              ----       */
	0x20, 0xFF, 0x83, 0x84, 0x85, 0xA0, 0x07, 0x86,
/* 0x48                      .     <     (     +     | */
	0x87, 0xA4, 0x9B, 0x2E, 0x3C, 0x28, 0x2B, 0x7C,
/* 0x50    &                                      ---- */
	0x26, 0x82, 0x88, 0x89, 0x8A, 0xA1, 0x8C, 0x07,
/* 0x58          ß     !     $     *     )     ;       */
	0x8D, 0xE1, 0x21, 0x24, 0x2A, 0x29, 0x3B, 0xAA,
/* 0x60    -     /  ----     Ä  ----  ----  ----       */
	0x2D, 0x2F, 0x07, 0x8E, 0x07, 0x07, 0x07, 0x8F,
/* 0x68             ----     ,     %     _     >     ? */
	0x80, 0xA5, 0x07, 0x2C, 0x25, 0x5F, 0x3E, 0x3F,
/* 0x70  ---        ----  ----  ----  ----  ----  ---- */
	0x07, 0x90, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
/* 0x78    *     `     :     #     @     '     =     " */
	0x70, 0x60, 0x3A, 0x23, 0x40, 0x27, 0x3D, 0x22,
/* 0x80    *     a     b     c     d     e     f     g */
	0x07, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
/* 0x88    h     i              ----  ----  ----       */
	0x68, 0x69, 0xAE, 0xAF, 0x07, 0x07, 0x07, 0xF1,
/* 0x90    °     j     k     l     m     n     o     p */
	0xF8, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70,
/* 0x98    q     r                    ----        ---- */
	0x71, 0x72, 0xA6, 0xA7, 0x91, 0x07, 0x92, 0x07,
/* 0xA0          ~     s     t     u     v     w     x */
	0xE6, 0x7E, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
/* 0xA8    y     z              ----  ----  ----  ---- */
	0x79, 0x7A, 0xAD, 0xAB, 0x07, 0x07, 0x07, 0x07,
/* 0xB0    ^                    ----     §  ----       */
	0x5E, 0x9C, 0x9D, 0xFA, 0x07, 0x07, 0x07, 0xAC,
/* 0xB8       ----     [     ]  ----  ----  ----  ---- */
	0xAB, 0x07, 0x5B, 0x5D, 0x07, 0x07, 0x07, 0x07,
/* 0xC0    {     A     B     C     D     E     F     G */
	0x7B, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
/* 0xC8    H     I  ----           ö              ---- */
	0x48, 0x49, 0x07, 0x93, 0x94, 0x95, 0xA2, 0x07,
/* 0xD0    }     J     K     L     M     N     O     P */
	0x7D, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50,
/* 0xD8    Q     R  ----           ü                   */
	0x51, 0x52, 0x07, 0x96, 0x81, 0x97, 0xA3, 0x98,
/* 0xE0    \           S     T     U     V     W     X */
	0x5C, 0xF6, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
/* 0xE8    Y     Z        ----     Ö  ----  ----  ---- */
	0x59, 0x5A, 0xFD, 0x07, 0x99, 0x07, 0x07, 0x07,
/* 0xF0    0     1     2     3     4     5     6     7 */
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
/* 0xF8    8     9  ----  ----     Ü  ----  ----  ---- */
	0x38, 0x39, 0x07, 0x07, 0x9A, 0x07, 0x07, 0x07
};

static inline void
vtoc_ebcdic_dec (const char *source, char *target, int l)
{
	int i;

	for (i = 0; i < l; i++)
		target[i]=(char)EBCtoASC[(unsigned char)(source[i])];
}

/*
 * compute the block number from a
 * cyl-cyl-head-head structure
 */
static inline uint64_t
cchh2blk (cchh_t *ptr, struct hd_geometry *geo)
{
	uint64_t cyl;
	uint16_t head;

	/*decode cylinder and heads for large volumes */
	cyl = ptr->hh & 0xFFF0;
	cyl <<= 12;
	cyl |= ptr->cc;
	head = ptr->hh & 0x000F;
	return cyl * geo->heads * geo->sectors +
	       head * geo->sectors;
}

/*
 * compute the block number from a
 * cyl-cyl-head-head-block structure
 */
static inline uint64_t
cchhb2blk (cchhb_t *ptr, struct hd_geometry *geo)
{
	uint64_t cyl;
	uint16_t head;

	/*decode cylinder and heads for large volumes */
	cyl = ptr->hh & 0xFFF0;
	cyl <<= 12;
	cyl |= ptr->cc;
	head = ptr->hh & 0x000F;
	return  cyl * geo->heads * geo->sectors +
		head * geo->sectors +
		ptr->b;
}

#endif /* _DASD_H */
