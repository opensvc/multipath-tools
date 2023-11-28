#ifndef _CLI_H_
#define _CLI_H_

#include <stdint.h>

/*
 * CLI commands consist of 4 bytes, a verb (byte 0) and up to
 * 3 qualifiers (byte 1 - 3).
 */

enum {
	/* See INVALID_FINGERPRINT in cli.c */
	KEY_INVALID		=  0,

	/* Verbs */
	VRB_LIST		=  1,
	VRB_ADD			=  2,
	VRB_DEL			=  3,
	VRB_RESET		=  4,
	VRB_SWITCH		=  5,
	VRB_RECONFIGURE		=  6,
	VRB_SUSPEND		=  7,
	VRB_RESUME		=  8,
	VRB_RESIZE		=  9,
	VRB_RELOAD		= 10,
	VRB_FAIL		= 11,
	VRB_REINSTATE		= 12,
	VRB_DISABLEQ		= 13,
	VRB_RESTOREQ		= 14,
	VRB_FORCEQ		= 15,
	VRB_GETPRSTATUS		= 16,
	VRB_SETPRSTATUS		= 17,
	VRB_UNSETPRSTATUS	= 18,
	VRB_GETPRKEY		= 19,
	VRB_SETPRKEY		= 20,
	VRB_UNSETPRKEY		= 21,
	VRB_SETMARGINAL		= 22,
	VRB_UNSETMARGINAL	= 23,
	VRB_SHUTDOWN		= 24,
	VRB_QUIT		= 25,

	/* Qualifiers, values must be different from verbs */
	KEY_PATH		= 65,
	KEY_PATHS		= 66,
	KEY_MAP			= 67,
	KEY_MAPS		= 68,
	KEY_TOPOLOGY		= 69,
	KEY_CONFIG		= 70,
	KEY_BLACKLIST		= 71,
	KEY_DEVICES		= 72,
	KEY_WILDCARDS		= 73,
	KEY_ALL			= 74,
	KEY_DAEMON		= 75,
	KEY_FMT			= 76,
	KEY_RAW			= 77,
	KEY_STATUS		= 78,
	KEY_STATS		= 79,
	KEY_JSON		= 80,
	KEY_LOCAL		= 81,
	KEY_GROUP		= 82,
	KEY_KEY			= 83,
	KEY_IF_IDLE		= 84,
};

/*
 * The shifted qualifiers determine valid positions of the
 * keywords in the known commands. E.g. the only qualifier
 * that's valid in position 3 is "fmt", e.g. "list maps raw fmt".
 */
enum {
	/* byte 1: qualifier 1 */
	Q1_PATH			= KEY_PATH << 8,
	Q1_PATHS		= KEY_PATHS << 8,
	Q1_MAP			= KEY_MAP << 8,
	Q1_MAPS			= KEY_MAPS << 8,
	Q1_TOPOLOGY		= KEY_TOPOLOGY << 8,
	Q1_CONFIG		= KEY_CONFIG << 8,
	Q1_BLACKLIST		= KEY_BLACKLIST << 8,
	Q1_DEVICES		= KEY_DEVICES << 8,
	Q1_WILDCARDS		= KEY_WILDCARDS << 8,
	Q1_ALL			= KEY_ALL << 8,
	Q1_DAEMON		= KEY_DAEMON << 8,
	Q1_STATUS		= KEY_STATUS << 8,
	Q1_IF_IDLE		= KEY_IF_IDLE << 8,

	/* byte 2: qualifier 2 */
	Q2_FMT			= KEY_FMT << 16,
	Q2_RAW			= KEY_RAW << 16,
	Q2_STATUS		= KEY_STATUS << 16,
	Q2_STATS		= KEY_STATS << 16,
	Q2_TOPOLOGY		= KEY_TOPOLOGY << 16,
	Q2_JSON			= KEY_JSON << 16,
	Q2_LOCAL		= KEY_LOCAL << 16,
	Q2_GROUP		= KEY_GROUP << 16,
	Q2_KEY			= KEY_KEY << 16,

	/* byte 3: qualifier 3 */
	Q3_FMT			= KEY_FMT << 24,
};

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
	uint8_t code;
	int has_param;
};

struct strbuf;

typedef int (cli_handler)(void *keywords, struct strbuf *reply, void *data);

struct handler {
	uint32_t fingerprint;
	int locked;
	cli_handler *fn;
};

int alloc_handlers (void);
int __set_handler_callback (uint32_t fp, cli_handler *fn, bool locked);
#define set_handler_callback(fp, fn) __set_handler_callback(fp, fn, true)
#define set_unlocked_handler_callback(fp, fn) __set_handler_callback(fp, fn, false)

int get_cmdvec (char *cmd, vector *v, bool allow_incomplete);
struct handler *find_handler_for_cmdvec(const struct _vector *v);
void genhelp_handler (const char *cmd, int error, struct strbuf *reply);

int load_keys (void);
char * get_keyparam (vector v, uint8_t code);
void free_key (struct key * kw);
void free_keys (vector vec);
void free_handlers (void);
int cli_init (void);
void cli_exit(void);
uint32_t fingerprint(const struct _vector *vec);
vector get_keys(void);
vector get_handlers(void);
struct key *find_key (const char * str);

#endif /* _CLI_H_ */
