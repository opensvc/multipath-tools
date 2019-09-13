#ifndef _CHECKERS_H
#define _CHECKERS_H

#include "list.h"
#include "memory.h"
#include "defaults.h"

/*
 *
 * Userspace (multipath/multipathd) path states
 *
 * PATH_WILD:
 * - Use: Any checker
 * - Description: Corner case where "fd < 0" for path fd (see checker_check()),
 *   or where a checker detects an unsupported device
 *   (e.g. wrong checker configured for a given device).
 *
 * PATH_UNCHECKED:
 * - Use: Only in directio checker
 * - Description: set when fcntl(F_GETFL) fails to return flags or O_DIRECT
 *   not include in flags, or O_DIRECT read fails
 * - Notes:
 *   - multipathd: uses it to skip over paths in sync_map_state()
 *   - multipath: used in update_paths(); if state==PATH_UNCHECKED, call
 *     pathinfo()
 *
 * PATH_DOWN:
 * - Use: All checkers (directio, emc_clariion, hp_sw, readsector0, tur)
 * - Description: Either a) SG_IO ioctl failed, or b) check condition on some
 *   SG_IO ioctls that succeed (tur, readsector0 checkers); path is down and
 *   you shouldn't try to send commands to it
 *
 * PATH_UP:
 * - Use: All checkers (directio, emc_clariion, hp_sw, readsector0, tur)
 * - Description: Path is up and I/O can be sent to it
 *
 * PATH_SHAKY:
 * - Use: Only emc_clariion
 * - Description: Indicates path not available for "normal" operations
 *
 * PATH_GHOST:
 * - Use: Only hp_sw and rdac
 * - Description: Indicates a "passive/standby" path on active/passive HP
 *   arrays.  These paths will return valid answers to certain SCSI commands
 *   (tur, read_capacity, inquiry, start_stop), but will fail I/O commands.
 *   The path needs an initialization command to be sent to it in order for
 *   I/Os to succeed.
 *
 * PATH_PENDING:
 * - Use: All async checkers
 * - Description: Indicates a check IO is in flight.
 *
 * PATH_TIMEOUT:
 * - Use: Only tur checker
 * - Description: Command timed out
 *
 * PATH REMOVED:
 * - Use: All checkers
 * - Description: Device has been removed from the system
 *
 * PATH_DELAYED:
 * - Use: None of the checkers (returned if the path is being delayed before
 *   reintegration.
 * - Description: If a path fails after being up for less than
 *   delay_watch_checks checks, when it comes back up again, it will not
 *   be marked as up until it has been up for delay_wait_checks checks.
 *   During this time, it is marked as "delayed"
 */
enum path_check_state {
	PATH_WILD,
	PATH_UNCHECKED,
	PATH_DOWN,
	PATH_UP,
	PATH_SHAKY,
	PATH_GHOST,
	PATH_PENDING,
	PATH_TIMEOUT,
	PATH_REMOVED,
	PATH_DELAYED,
	PATH_MAX_STATE
};

#define DIRECTIO     "directio"
#define TUR          "tur"
#define HP_SW        "hp_sw"
#define RDAC         "rdac"
#define EMC_CLARIION "emc_clariion"
#define READSECTOR0  "readsector0"
#define CCISS_TUR    "cciss_tur"
#define NONE         "none"

#define ASYNC_TIMEOUT_SEC	30

/*
 * strings lengths
 */
#define CHECKER_NAME_LEN 16
#define CHECKER_MSG_LEN 256
#define CHECKER_DEV_LEN 256
#define LIB_CHECKER_NAMELEN 256

/*
 * Generic message IDs for use in checkers.
 */
enum {
	CHECKER_MSGID_NONE = 0,
	CHECKER_MSGID_DISABLED,
	CHECKER_MSGID_NO_FD,
	CHECKER_MSGID_INVALID,
	CHECKER_MSGID_UP,
	CHECKER_MSGID_DOWN,
	CHECKER_MSGID_GHOST,
	CHECKER_MSGID_UNSUPPORTED,
	CHECKER_GENERIC_MSGTABLE_SIZE,
	CHECKER_FIRST_MSGID = 100,	/* lowest msgid for checkers */
	CHECKER_MSGTABLE_SIZE = 100,	/* max msg table size for checkers */
};

struct checker_class;
struct checker {
	struct checker_class *cls;
	int fd;
	unsigned int timeout;
	int disable;
	short msgid;		             /* checker-internal extra status */
	void * context;                      /* store for persistent data */
	void ** mpcontext;                   /* store for persistent data shared
						multipath-wide. Use MALLOC if
						you want to stuff data in. */
};

static inline int checker_selected(const struct checker *c)
{
	return c != NULL && c->cls != NULL;
}

const char *checker_state_name(int);
int init_checkers(const char *);
void cleanup_checkers (void);
int checker_init (struct checker *, void **);
int checker_mp_init(struct checker *, void **);
void checker_clear (struct checker *);
void checker_put (struct checker *);
void checker_reset (struct checker *);
void checker_set_sync (struct checker *);
void checker_set_async (struct checker *);
void checker_set_fd (struct checker *, int);
void checker_enable (struct checker *);
void checker_disable (struct checker *);
int checker_check (struct checker *, int);
int checker_is_sync(const struct checker *);
const char *checker_name (const struct checker *);
/*
 * This returns a string that's best prepended with "$NAME checker",
 * where $NAME is the return value of checker_name().
 */
const char *checker_message(const struct checker *);
void checker_clear_message (struct checker *c);
void checker_get(const char *, struct checker *, const char *);

/* Prototypes for symbols exported by path checker dynamic libraries (.so) */
int libcheck_check(struct checker *);
int libcheck_init(struct checker *);
void libcheck_free(struct checker *);
/*
 * msgid => message map.
 *
 * It only needs to be provided if the checker defines specific
 * message IDs.
 * Message IDs available to checkers start at CHECKER_FIRST_MSG.
 * The msgtable array is 0-based, i.e. msgtable[0] is the message
 * for msgid == __CHECKER_FIRST_MSG.
 * The table ends with a NULL element.
 */
extern const char *libcheck_msgtable[];

#endif /* _CHECKERS_H */
