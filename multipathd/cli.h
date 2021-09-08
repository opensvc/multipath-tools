#ifndef _CLI_H_
#define _CLI_H_

#include <stdint.h>

enum {
	__LIST,			/*  0 */
	__ADD,
	__DEL,
	__SWITCH,
	__SUSPEND,
	__RESUME,			/*  5 */
	__REINSTATE,
	__FAIL,
	__RESIZE,
	__RESET,
	__RELOAD,			/* 10 */
	__FORCEQ,
	__DISABLEQ,
	__RESTOREQ,
	__PATHS,
	__MAPS,			/* 15 */
	__PATH,
	__MAP,
	__GROUP,
	__RECONFIGURE,
	__DAEMON,			/* 20 */
	__STATUS,
	__STATS,
	__TOPOLOGY,
	__CONFIG,
	__BLACKLIST,			/* 25 */
	__DEVICES,
	__RAW,
	__WILDCARDS,
	__QUIT,
	__SHUTDOWN,			/* 30 */
	__GETPRSTATUS,
	__SETPRSTATUS,
	__UNSETPRSTATUS,
	__FMT,
	__JSON,			/* 35 */
	__GETPRKEY,
	__SETPRKEY,
	__UNSETPRKEY,
	__KEY,
	__LOCAL,			/* 40 */
	__SETMARGINAL,
	__UNSETMARGINAL,
};

#define LIST		(1ULL << __LIST)
#define ADD		(1ULL << __ADD)
#define DEL		(1ULL << __DEL)
#define SWITCH		(1ULL << __SWITCH)
#define SUSPEND	(1ULL << __SUSPEND)
#define RESUME		(1ULL << __RESUME)
#define REINSTATE	(1ULL << __REINSTATE)
#define FAIL		(1ULL << __FAIL)
#define RESIZE		(1ULL << __RESIZE)
#define RESET		(1ULL << __RESET)
#define RELOAD		(1ULL << __RELOAD)
#define FORCEQ		(1ULL << __FORCEQ)
#define DISABLEQ	(1ULL << __DISABLEQ)
#define RESTOREQ	(1ULL << __RESTOREQ)
#define PATHS		(1ULL << __PATHS)
#define MAPS		(1ULL << __MAPS)
#define PATH		(1ULL << __PATH)
#define MAP		(1ULL << __MAP)
#define GROUP		(1ULL << __GROUP)
#define RECONFIGURE	(1ULL << __RECONFIGURE)
#define DAEMON		(1ULL << __DAEMON)
#define STATUS		(1ULL << __STATUS)
#define STATS		(1ULL << __STATS)
#define TOPOLOGY	(1ULL << __TOPOLOGY)
#define CONFIG		(1ULL << __CONFIG)
#define BLACKLIST	(1ULL << __BLACKLIST)
#define DEVICES	(1ULL << __DEVICES)
#define RAW		(1ULL << __RAW)
#define COUNT		(1ULL << __COUNT)
#define WILDCARDS	(1ULL << __WILDCARDS)
#define QUIT		(1ULL << __QUIT)
#define SHUTDOWN	(1ULL << __SHUTDOWN)
#define GETPRSTATUS	(1ULL << __GETPRSTATUS)
#define SETPRSTATUS	(1ULL << __SETPRSTATUS)
#define UNSETPRSTATUS	(1ULL << __UNSETPRSTATUS)
#define FMT		(1ULL << __FMT)
#define JSON		(1ULL << __JSON)
#define GETPRKEY	(1ULL << __GETPRKEY)
#define SETPRKEY	(1ULL << __SETPRKEY)
#define UNSETPRKEY	(1ULL << __UNSETPRKEY)
#define KEY		(1ULL << __KEY)
#define LOCAL		(1ULL << __LOCAL)
#define SETMARGINAL	(1ULL << __SETMARGINAL)
#define UNSETMARGINAL	(1ULL << __UNSETMARGINAL)

#define INITIAL_REPLY_LEN	1200

#define REALLOC_REPLY(r, a, m)					\
	do {							\
		if ((a)) {					\
			char *tmp = (r);			\
								\
			if (m >= MAX_REPLY_LEN) {		\
				condlog(1, "Warning: max reply length exceeded"); \
				free(tmp);			\
				(r) = NULL;			\
			} else {				\
				(r) = realloc((r), (m) * 2);	\
				if ((r)) {			\
					memset((r) + (m), 0, (m)); \
					(m) *= 2;		\
				}				\
				else				\
					free(tmp);		\
			}					\
		}						\
	} while (0)

struct key {
	char * str;
	char * param;
	uint64_t code;
	int has_param;
};

struct strbuf;

typedef int (cli_handler)(void *keywords, struct strbuf *reply, void *data);

struct handler {
	uint64_t fingerprint;
	int locked;
	cli_handler *fn;
};

int alloc_handlers (void);
int __set_handler_callback (uint64_t fp, cli_handler *fn, bool locked);
#define set_handler_callback(fp, fn) __set_handler_callback(fp, fn, true)
#define set_unlocked_handler_callback(fp, fn) __set_handler_callback(fp, fn, false)

int get_cmdvec (char *cmd, vector *v);
struct handler *find_handler_for_cmdvec(const struct _vector *v);
void genhelp_handler (const char *cmd, int error, struct strbuf *reply);

int load_keys (void);
char * get_keyparam (vector v, uint64_t code);
void free_keys (vector vec);
void free_handlers (void);
int cli_init (void);
void cli_exit(void);
char * key_generator (const char * str, int state);

#endif /* _CLI_H_ */
