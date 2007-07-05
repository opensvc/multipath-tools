#ifndef _CHECKERS_H
#define _CHECKERS_H

/*
 *
 * Userspace (multipath/multipathd) path states
 *
 * PATH_WILD:
 * - Use: None of the checkers (returned if we don't have an fd)
 * - Description: Corner case where "fd <= 0" for path fd (see checker_check())
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
 * - Use: Only hp_sw
 * - Description: Indicates a "passive/standby" path on active/passive HP
 *   arrays.  These paths will return valid answers to certain SCSI commands
 *   (tur, read_capacity, inquiry, start_stop), but will fail I/O commands.
 *   The path needs an initialization command to be sent to it in order for
 *   I/Os to succeed.
 *
 * PATH_PENDING:
 * - Use: All async checkers
 * - Description: Indicates a check IO is in flight.
 */
#define PATH_WILD	-1
#define PATH_UNCHECKED	0
#define PATH_DOWN	1
#define PATH_UP		2
#define PATH_SHAKY	3
#define PATH_GHOST	4
#define PATH_PENDING	5

#define DIRECTIO     "directio"
#define TUR          "tur"
#define HP_SW        "hp_sw"
#define RDAC         "rdac"
#define EMC_CLARIION "emc_clariion"
#define READSECTOR0  "readsector0"

#define DEFAULT_CHECKER DIRECTIO

/*
 * Overloaded storage response time can be very long.
 * SG_IO timouts after DEF_TIMEOUT milliseconds, and checkers interprets this
 * as a path failure. multipathd then proactively evicts the path from the DM
 * multipath table in this case.
 *
 * This generaly snow balls and ends up in full eviction and IO errors for end
 * users. Bad. This may also cause SCSI bus resets, causing disruption for all
 * local and external storage hardware users.
 * 
 * Provision a long timeout. Longer than any real-world application would cope
 * with.
 */
#define DEF_TIMEOUT		300000
#define ASYNC_TIMEOUT_SEC	30

/*
 * strings lengths
 */
#define CHECKER_NAME_LEN 16
#define CHECKER_MSG_LEN 256
#define CHECKER_DEV_LEN 256

struct checker {
	int fd;
	int sync;
	char name[CHECKER_NAME_LEN];
	char message[CHECKER_MSG_LEN];       /* comm with callers */
	void * context;                      /* store for persistent data */
	void ** mpcontext;                   /* store for persistent data
						shared multipath-wide */
	int (*check)(struct checker *);
	int (*init)(struct checker *);       /* to allocate the context */
	void (*free)(struct checker *);      /* to free the context */
};

#define MSG(c, fmt, args...) snprintf((c)->message, CHECKER_MSG_LEN, fmt, ##args);

int checker_init (struct checker *, void **);
void checker_put (struct checker *);
void checker_reset (struct checker * c);
void checker_set_sync (struct checker * c);
void checker_set_async (struct checker * c);
void checker_set_fd (struct checker *, int);
struct checker * checker_lookup (char *);
int checker_check (struct checker *);
int checker_selected (struct checker *);
char * checker_name (struct checker *);
char * checker_message (struct checker *);
struct checker * checker_default (void);
void checker_get (struct checker *, struct checker *);

#endif /* _CHECKERS_H */
