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
 * - Use: None of the checkers (returned if we don't have an fd)
 * - Description: Corner case where "fd < 0" for path fd (see checker_check())
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
#define RBD          "rbd"

#define ASYNC_TIMEOUT_SEC	30

/*
 * strings lengths
 */
#define CHECKER_NAME_LEN 16
#define CHECKER_MSG_LEN 256
#define CHECKER_DEV_LEN 256
#define LIB_CHECKER_NAMELEN 256

struct checker {
	struct list_head node;
	void *handle;
	int refcount;
	int fd;
	int sync;
	unsigned int timeout;
	int disable;
	char name[CHECKER_NAME_LEN];
	char message[CHECKER_MSG_LEN];       /* comm with callers */
	void * context;                      /* store for persistent data */
	void ** mpcontext;                   /* store for persistent data shared
						multipath-wide. Use MALLOC if
						you want to stuff data in. */
	int (*check)(struct checker *);
	void (*repair)(struct checker *);    /* called if check returns
						PATH_DOWN to bring path into
						usable state */
	int (*init)(struct checker *);       /* to allocate the context */
	void (*free)(struct checker *);      /* to free the context */
};

#define MSG(c, fmt, args...) snprintf((c)->message, CHECKER_MSG_LEN, fmt, ##args);

char * checker_state_name (int);
int init_checkers (char *);
void cleanup_checkers (void);
struct checker * add_checker (char *, char *);
struct checker * checker_lookup (char *);
int checker_init (struct checker *, void **);
void checker_clear (struct checker *);
void checker_put (struct checker *);
void checker_reset (struct checker *);
void checker_set_sync (struct checker *);
void checker_set_async (struct checker *);
void checker_set_fd (struct checker *, int);
void checker_enable (struct checker *);
void checker_disable (struct checker *);
void checker_repair (struct checker *);
int checker_check (struct checker *, int);
int checker_selected (struct checker *);
char * checker_name (struct checker *);
char * checker_message (struct checker *);
void checker_clear_message (struct checker *c);
void checker_get (char *, struct checker *, char *);

/* Functions exported by path checker dynamic libraries (.so) */
int libcheck_check(struct checker *);
int libcheck_init(struct checker *);
void libcheck_free(struct checker *);
void libcheck_repair(struct checker *);

#endif /* _CHECKERS_H */
