/* version - 1.0 */

#ifndef MPATH_PERSIST_LIB_H
#define MPATH_PERSIST_LIB_H


#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>

#define MPATH_MAX_PARAM_LEN	8192

#define MPATH_MX_TIDS		32	  /* Max number of transport ids"*/
#define MPATH_MX_TID_LEN	256	  /* Max length of transport id */

/* PRIN Service Actions */
#define MPATH_PRIN_RKEY_SA	0x00	   /* READ KEYS SA*/
#define MPATH_PRIN_RRES_SA	0x01	   /* READ RESERVATION  SA*/
#define MPATH_PRIN_RCAP_SA	0x02	   /* REPORT CAPABILITIES SA*/
#define MPATH_PRIN_RFSTAT_SA	0x03	   /* READ FULL STATUS SA*/

/* PROUT Service Actions */
#define MPATH_PROUT_REG_SA	0x00	    /* REGISTER SA */
#define MPATH_PROUT_RES_SA	0x01	    /* RESERVE SA*/
#define MPATH_PROUT_REL_SA	0x02	    /* RELEASE SA*/
#define MPATH_PROUT_CLEAR_SA	0x03	    /* CLEAR SA*/
#define MPATH_PROUT_PREE_SA	0x04	    /* PREEMPT SA*/
#define MPATH_PROUT_PREE_AB_SA	0x05	    /* PREEMPT AND ABORT SA*/
#define MPATH_PROUT_REG_IGN_SA	0x06	    /* REGISTER AND IGNORE EXISTING KEY SA*/
#define MPATH_PROUT_REG_MOV_SA	0x07	    /* REGISTER AND MOVE SA*/

#define MPATH_LU_SCOPE		0x00	    /* LU_SCOPE */

/* Persistent reservations type */
#define MPATH_PRTPE_WE		0x01	    /* Write Exclusive */
#define MPATH_PRTPE_EA		0x03	    /* Exclusive Access*/
#define MPATH_PRTPE_WE_RO	0x05	    /* WriteExclusive Registrants Only */
#define MPATH_PRTPE_EA_RO	0x06	    /* Exclusive Access. Registrants Only*/
#define MPATH_PRTPE_WE_AR	0x07	    /* Write Exclusive. All Registrants*/
#define MPATH_PRTPE_EA_AR	0x08	    /* Exclusive Access. All Registrants */


/* PR RETURN_STATUS */
#define MPATH_PR_SKIP			-1  /* skipping this path */
#define MPATH_PR_SUCCESS		0
#define MPATH_PR_SYNTAX_ERROR		1   /*  syntax error or invalid parameter */
					    /* status for check condition */
#define MPATH_PR_SENSE_NOT_READY	2   /*	[sk,asc,ascq: 0x2,*,*] */
#define MPATH_PR_SENSE_MEDIUM_ERROR	3   /*	[sk,asc,ascq: 0x3,*,*] */
#define MPATH_PR_SENSE_HARDWARE_ERROR	4   /*	[sk,asc,ascq: 0x4,*,*] */
#define MPATH_PR_ILLEGAL_REQ		5   /*	[sk,asc,ascq: 0x5,*,*]*/
#define MPATH_PR_SENSE_UNIT_ATTENTION	6   /*	[sk,asc,ascq: 0x6,*,*] */
#define MPATH_PR_SENSE_INVALID_OP	7   /*	[sk,asc,ascq: 0x5,0x20,0x0]*/
#define MPATH_PR_SENSE_ABORTED_COMMAND  8   /*  [sk,asc,ascq: 0xb,*,*] */
#define MPATH_PR_NO_SENSE		9   /*	[sk,asc,ascq: 0x0,*,*] */

#define MPATH_PR_SENSE_MALFORMED	10  /* Response to SCSI command malformed */
#define MPATH_PR_RESERV_CONFLICT	11  /* Reservation conflict on the device */
#define MPATH_PR_FILE_ERROR		12  /* file (device node) problems(e.g. not found)*/
#define MPATH_PR_DMMP_ERROR		13  /* DMMP related error.(e.g Error in getting dm info */
#define MPATH_PR_THREAD_ERROR		14  /* pthreads error (e.g. unable to create new thread) */
#define MPATH_PR_OTHER			15  /*other error/warning has occurred(transport
					      or driver error) */

/* PR MASK */
#define MPATH_F_APTPL_MASK		0x01	/* APTPL MASK*/
#define MPATH_F_ALL_TG_PT_MASK		0x04	/* ALL_TG_PT MASK*/
#define MPATH_F_SPEC_I_PT_MASK		0x08	/* SPEC_I_PT MASK*/
#define MPATH_PR_TYPE_MASK		0x0f	/* TYPE MASK*/
#define MPATH_PR_SCOPE_MASK		0xf0	/* SCOPE MASK*/

/*Transport ID PROTOCOL IDENTIFIER values */
#define MPATH_PROTOCOL_ID_FC		0x00
#define MPATH_PROTOCOL_ID_ISCSI		0x05
#define MPATH_PROTOCOL_ID_SAS		0x06


/*Transport ID FORMATE CODE */
#define MPATH_WWUI_DEVICE_NAME		0x00	/* World wide unique initiator device name */
#define MPATH_WWUI_PORT_IDENTIFIER	0x40	/* World wide unique initiator port identifier	*/



extern unsigned int mpath_mx_alloc_len;



struct prin_readdescr
{
	uint32_t prgeneration;
	uint32_t additional_length;	/* The value should be either 0 or divisible by 8.
					   0 indicates no registered reservation key. */
	uint8_t	 key_list[MPATH_MAX_PARAM_LEN];
};

struct prin_resvdescr
{
	uint32_t prgeneration;
	uint32_t additional_length;	/* The value should be either 0 or 10h. 0 indicates
					   there is no reservation held. 10h indicates the
					   key[8] and scope_type have valid values */
	uint8_t  key[8];
	uint32_t _obsolete;
	uint8_t  _reserved;
	uint8_t  scope_type;            /* Use PR SCOPE AND TYPE MASK specified above */
	uint16_t _obsolete1;
};

struct prin_capdescr
{
	uint16_t length;
	uint8_t  flags[2];
	uint16_t pr_type_mask;
	uint16_t _reserved;
};

struct transportid
{
	uint8_t format_code;
	uint8_t protocol_id;
	union {
		uint8_t n_port_name[8];	/* FC transport*/
		uint8_t sas_address[8];	/* SAS transport */
		uint8_t iscsi_name[256]; /* ISCSI  transport */
	};
};

struct prin_fulldescr
{
	uint8_t key[8];
	uint8_t flag;			/* All_tg_pt and reservation holder */
	uint8_t scope_type;		/* Use PR SCOPE AND TYPE MASK specified above.
					   Meaningful only for reservation holder */
	uint16_t rtpi;
	struct transportid trnptid;
};

struct print_fulldescr_list
{
	uint32_t prgeneration;
	uint32_t number_of_descriptor;
	uint8_t private_buffer[MPATH_MAX_PARAM_LEN]; /*Private buffer for list storage*/
	struct prin_fulldescr *descriptors[];
};

struct prin_resp
{
	union
	{
		struct prin_readdescr prin_readkeys; /* for PRIN read keys SA*/
		struct prin_resvdescr prin_readresv; /* for PRIN read reservation SA*/
		struct prin_capdescr  prin_readcap;  /* for PRIN Report Capabilities SA*/
		struct print_fulldescr_list prin_readfd;   /* for PRIN read full status SA*/
	}prin_descriptor;
};

struct prout_param_descriptor {		/* PROUT parameter descriptor */
	uint8_t	 key[8];
	uint8_t	 sa_key[8];
	uint32_t _obsolete;
	uint8_t	 sa_flags;
	uint8_t _reserved;
	uint16_t _obsolete1;
	uint8_t  private_buffer[MPATH_MAX_PARAM_LEN]; /*private buffer for list storage*/
	uint32_t num_transportid;	/* Number of Transport ID listed in trnptid_list[]*/
	struct transportid *trnptid_list[];
};


/* Function declarations */

/*
 * DESCRIPTION :
 *	Initialize device mapper multipath configuration. This function must be invoked first
 *	before performing reservation management functions.
 *	Either this function or mpath_lib_init() may be used.
 *	Use this function to work with libmultipath's internal "struct config"
 *	and "struct udev". The latter will be initialized automatically.
 *	Call libmpathpersist_exit() for cleanup.
 * RESTRICTIONS:
 *
 * RETURNS: 0->Success, 1->Failed.
 */
extern int libmpathpersist_init (void);

/*
 * DESCRIPTION :
 *	Initialize device mapper multipath configuration. This function must be invoked first
 *	before performing reservation management functions.
 *	Either this function or libmpathpersist_init() may be used.
 *	Use this function to work with an application-specific "struct config"
 *	and "struct udev". The latter must be initialized by the application.
 *	Call mpath_lib_exit() for cleanup.
 * RESTRICTIONS:
 *
 * RETURNS: struct config ->Success, NULL->Failed.
 */
extern struct config * mpath_lib_init (void);


/*
 * DESCRIPTION :
 *	Release device mapper multipath configuration. This function must be invoked after
 *	performing reservation management functions.
 *	Use this after initialization with mpath_lib_init().
 * RESTRICTIONS:
 *
 * RETURNS: 0->Success, 1->Failed.
 */
extern int mpath_lib_exit (struct config *conf);

/*
 * DESCRIPTION :
 *	Release device mapper multipath configuration a. This function must be invoked after
 *	performing reservation management functions.
 *	Use this after initialization with libmpathpersist_init().
 *	Calling libmpathpersist_init() after libmpathpersist_exit() will fail.
 * RESTRICTIONS:
 *
 * RETURNS: 0->Success, 1->Failed.
 */
extern int libmpathpersist_exit (void);


/*
 * DESCRIPTION :
 * This function sends PRIN command to the DM device and get the response.
 *
 * @fd:	The file descriptor of a multipath device. Input argument.
 * @rq_servact: PRIN command service action. Input argument
 * @resp: The response from PRIN service action. The resp is a struct specified above. The caller should
 *	manage the memory allocation of this struct
 * @noisy: Turn on debugging trace: Input argument. 0->Disable, 1->Enable
 * @verbose: Set verbosity level. Input argument. value:[0-3]. 0->disabled, 3->Max verbose
 *
 * RESTRICTIONS:
 *
 * RETURNS: MPATH_PR_SUCCESS if PR command successful else returns any of the status specified
 *       above in RETURN_STATUS.
 *
 */
extern int mpath_persistent_reserve_in (int fd, int rq_servact, struct prin_resp *resp,
		int noisy, int verbose);

/*
 * DESCRIPTION :
 * This function is like mpath_persistent_reserve_in(), except that it
 * requires mpath_persistent_reserve_init_vecs() to be called before the
 * PR call to set up internal variables. These must later be cleanup up
 * by calling mpath_persistent_reserve_free_vecs().
 *
 * RESTRICTIONS:
 * This function uses static internal variables, and is not thread-safe.
 */
extern int __mpath_persistent_reserve_in(int fd, int rq_servact,
		struct prin_resp *resp, int noisy);

/*
 * DESCRIPTION :
 * This function sends PROUT command to the DM device and get the response.
 *
 * @fd: The file descriptor of a multipath device. Input argument.
 * @rq_servact: PROUT command service action. Input argument
 * @rq_scope: Persistent reservation scope. The value should be always LU_SCOPE (0h).
 * @rq_type: Persistent reservation type. The valid values of persistent reservation types are
 *	5h (Write exclusive - registrants only)
 *	6h (Exclusive access - registrants only)
 *	7h (Write exclusive - All registrants)
 *	8h (Exclusive access - All registrants).
 * @paramp: PROUT command parameter data. The paramp is a struct which describes PROUT
 *	    parameter list. The caller should manage the memory allocation of this struct.
 * @noisy: Turn on debugging trace: Input argument.0->Disable, 1->Enable.
 * @verbose: Set verbosity level. Input argument. value:0 to 3. 0->disabled, 3->Max verbose
 *
 * RESTRICTIONS:
 *
 * RETURNS: MPATH_PR_SUCCESS if PR command successful else returns any of the status specified
 *       above in RETURN_STATUS.
 */
extern int mpath_persistent_reserve_out ( int fd, int rq_servact, int rq_scope,
		unsigned int rq_type, struct prout_param_descriptor *paramp, int noisy,
		int verbose);
/*
 * DESCRIPTION :
 * This function is like mpath_persistent_reserve_out(), except that it
 * requires mpath_persistent_reserve_init_vecs() to be called before the
 * PR call to set up internal variables. These must later be cleanup up
 * by calling mpath_persistent_reserve_free_vecs().
 *
 * RESTRICTIONS:
 * This function uses static internal variables, and is not thread-safe.
 */
extern int __mpath_persistent_reserve_out( int fd, int rq_servact, int rq_scope,
		unsigned int rq_type, struct prout_param_descriptor *paramp,
		int noisy);

/*
 * DESCRIPTION :
 * This function allocates data structures and performs basic initialization and
 * device discovery for later calls of __mpath_persistent_reserve_in() or
 * __mpath_persistent_reserve_out().
 * @verbose: Set verbosity level. Input argument. value:0 to 3. 0->disabled, 3->Max verbose
 *
 * RESTRICTIONS:
 * This function uses static internal variables, and is not thread-safe.
 *
 * RETURNS: MPATH_PR_SUCCESS if successful else returns any of the status specified
 *       above in RETURN_STATUS.
 */
int mpath_persistent_reserve_init_vecs(int verbose);

/*
 * DESCRIPTION :
 * This function frees data structures allocated by
 * mpath_persistent_reserve_init_vecs().
 *
 * RESTRICTIONS:
 * This function uses static internal variables, and is not thread-safe.
 */
void mpath_persistent_reserve_free_vecs(void);


#ifdef __cplusplus
}
#endif

#endif  /*MPATH_PERSIST_LIB_H*/
