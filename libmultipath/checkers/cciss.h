#ifndef CCISS_H
#define CCISS_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define CCISS_IOC_MAGIC 'B'

/*
 * transfer direction
 */
#define XFER_NONE		0x00
#define XFER_WRITE		0x01
#define XFER_READ		0x02
#define XFER_RSVD		0x03

/*
 * task attribute
 */
#define ATTR_UNTAGGED		0x00
#define ATTR_SIMPLE		0x04
#define ATTR_HEADOFQUEUE	0x05
#define ATTR_ORDERED		0x06
#define ATTR_ACA		0x07

/*
 * cdb type
 */
#define TYPE_CMD		0x00
#define TYPE_MSG		0x01

#define SENSEINFOBYTES		32

/*
 * Type defs used in the following structs
 */
#define BYTE __u8
#define WORD __u16
#define HWORD __u16
#define DWORD __u32

#pragma pack(1)

//Command List Structure
typedef union _SCSI3Addr_struct {
	struct {
		BYTE Dev;
		BYTE Bus:6;
		BYTE Mode:2;        // b00
	} PeripDev;
	struct {
		BYTE DevLSB;
		BYTE DevMSB:6;
		BYTE Mode:2;        // b01
	} LogDev;
	struct {
		BYTE Dev:5;
		BYTE Bus:3;
		BYTE Targ:6;
		BYTE Mode:2;        // b10
	} LogUnit;
} SCSI3Addr_struct;

typedef struct _PhysDevAddr_struct {
	DWORD             TargetId:24;
	DWORD             Bus:6;
	DWORD             Mode:2;
	SCSI3Addr_struct  Target[2]; //2 level target device addr
} PhysDevAddr_struct;

typedef struct _LogDevAddr_struct {
	DWORD            VolId:30;
	DWORD            Mode:2;
	BYTE             reserved[4];
} LogDevAddr_struct;

typedef union _LUNAddr_struct {
	BYTE               LunAddrBytes[8];
	SCSI3Addr_struct   SCSI3Lun[4];
	PhysDevAddr_struct PhysDev;
	LogDevAddr_struct  LogDev;
} LUNAddr_struct;

typedef struct _RequestBlock_struct {
	BYTE   CDBLen;
	struct {
		BYTE Type:3;
		BYTE Attribute:3;
		BYTE Direction:2;
	} Type;
	HWORD  Timeout;
	BYTE   CDB[16];
} RequestBlock_struct;

typedef union _MoreErrInfo_struct{
	struct {
		BYTE  Reserved[3];
		BYTE  Type;
		DWORD ErrorInfo;
	} Common_Info;
	struct{
		BYTE  Reserved[2];
		BYTE  offense_size;//size of offending entry
		BYTE  offense_num; //byte # of offense 0-base
		DWORD offense_value;
	} Invalid_Cmd;
} MoreErrInfo_struct;

typedef struct _ErrorInfo_struct {
	BYTE               ScsiStatus;
	BYTE               SenseLen;
	HWORD              CommandStatus;
	DWORD              ResidualCnt;
	MoreErrInfo_struct MoreErrInfo;
	BYTE               SenseInfo[SENSEINFOBYTES];
} ErrorInfo_struct;

#pragma pack()

typedef struct _IOCTL_Command_struct {
	LUNAddr_struct		LUN_info;
	RequestBlock_struct	Request;
	ErrorInfo_struct	error_info;
	WORD			buf_size;  /* size in bytes of the buf */
	BYTE			*buf;
} IOCTL_Command_struct;

typedef struct _LogvolInfo_struct{
	__u32   LunID;
	int     num_opens;  /* number of opens on the logical volume */
	int     num_parts;  /* number of partitions configured on logvol */
} LogvolInfo_struct;

#define CCISS_PASSTHRU     _IOWR(CCISS_IOC_MAGIC, 11, IOCTL_Command_struct)
#define CCISS_GETLUNINFO   _IOR(CCISS_IOC_MAGIC, 17, LogvolInfo_struct)

int cciss_init( struct checker *);
void cciss_free (struct checker * c);
int cciss_tur( struct checker *);

#endif
