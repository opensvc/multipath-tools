#define MPATH_XFER_HOST_DEV              0   /*data transfer from initiator to target */
#define MPATH_XFER_DEV_HOST              1   /*data transfer from target to initiator */
#define MPATH_XFER_NONE                  2   /*no data transfer */
#define MPATH_XFER_UNKNOWN               3   /*data transfer direction is unknown */

#if 0
static const char * pr_type_strs[] = {
	"obsolete [0]",
	"Write Exclusive",
	"obsolete [2]",
	"Exclusive Access",
	"obsolete [4]",
	"Write Exclusive, registrants only",
	"Exclusive Access, registrants only",
	"Write Exclusive, all registrants",
	"Exclusive Access, all registrants",
	"obsolete [9]", "obsolete [0xa]", "obsolete [0xb]", "obsolete [0xc]",
	"obsolete [0xd]", "obsolete [0xe]", "obsolete [0xf]",
};
#endif

typedef unsigned int     LWORD;     /* unsigned numeric, bit patterns */
typedef unsigned char    BYTE;      /* unsigned numeric, bit patterns */

typedef struct SenseData
{
	BYTE        Error_Code;
	BYTE        Segment_Number; /* not applicable to DAC */
	BYTE        Sense_Key;
	BYTE        Information[ 4 ];
	BYTE        Additional_Len;
	LWORD       Command_Specific_Info;
	BYTE        ASC;
	BYTE        ASCQ;
	BYTE        Field_Replaceable_Unit;
	BYTE        Sense_Key_Specific_Info[ 3 ];
	BYTE        Recovery_Action[ 2 ];
	BYTE        Total_Errors;
	BYTE        Total_Retries;
	BYTE        ASC_Stack_1;
	BYTE        ASCQ_Stack_1;
	BYTE        ASC_Stack_2;
	BYTE        ASCQ_Stack_2;
	BYTE        Additional_FRU_Info[ 8 ];
	BYTE        Error_Specific_Info[ 3 ];
	BYTE        Error_Detection_Point[ 4 ];
	BYTE        Original_CDB[10];
	BYTE        Host_ID;
	BYTE        Host_Descriptor[ 2 ];
	BYTE        Serial_Number[ 16 ];
	BYTE        Array_SW_Revision[ 4 ];
	BYTE        Data_Xfer_Operation;
	BYTE        LUN_Number;
	BYTE        LUN_Status;
	BYTE        Drive_ID;
	BYTE        Xfer_Start_Drive_ID;
	BYTE        Drive_SW_Revision[ 4 ];
	BYTE        Drive_Product_ID[ 16 ];
	BYTE        PowerUp_Status[ 2 ];
	BYTE        RAID_Level;
	BYTE        Drive_Sense_ID[ 2 ];
	BYTE        Drive_Sense_Data[ 32 ];
	BYTE        Reserved2[24];
} SenseData_t;

#define MPATH_PRIN_CMD 0x5e
#define MPATH_PRIN_CMDLEN 10
#define MPATH_PROUT_CMD 0x5f
#define MPATH_PROUT_CMDLEN 10

#define  DID_OK	0x00
/*
 *  Status codes
 */
#define SAM_STAT_GOOD            0x00
#define SAM_STAT_CHECK_CONDITION 0x02
#define SAM_STAT_CONDITION_MET   0x04
#define SAM_STAT_BUSY            0x08
#define SAM_STAT_INTERMEDIATE    0x10
#define SAM_STAT_INTERMEDIATE_CONDITION_MET 0x14
#define SAM_STAT_RESERVATION_CONFLICT 0x18
#define SAM_STAT_COMMAND_TERMINATED 0x22        /* obsolete in SAM-3 */
#define SAM_STAT_TASK_SET_FULL   0x28
#define SAM_STAT_ACA_ACTIVE      0x30
#define SAM_STAT_TASK_ABORTED    0x40

#define STATUS_MASK          0x3e

/*
 *  SENSE KEYS
 */

#define NO_SENSE            0x00
#define RECOVERED_ERROR     0x01
#define NOT_READY           0x02
#define MEDIUM_ERROR        0x03
#define HARDWARE_ERROR      0x04
#define ILLEGAL_REQUEST     0x05
#define UNIT_ATTENTION      0x06
#define DATA_PROTECT        0x07
#define BLANK_CHECK         0x08
#define COPY_ABORTED        0x0a
#define ABORTED_COMMAND     0x0b
#define VOLUME_OVERFLOW     0x0d
#define MISCOMPARE          0x0e


/* Driver status */
#define DRIVER_OK 0x00
