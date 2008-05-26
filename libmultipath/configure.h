/*
 * configurator actions
 */
#define ACT_NOTHING_STR         "unchanged"
#define ACT_REJECT_STR          "reject"
#define ACT_RELOAD_STR          "reload"
#define ACT_SWITCHPG_STR        "switchpg"
#define ACT_RENAME_STR          "rename"
#define ACT_CREATE_STR          "create"

enum actions {
	ACT_UNDEF,
	ACT_NOTHING,
	ACT_REJECT,
	ACT_RELOAD,
	ACT_SWITCHPG,
	ACT_RENAME,
	ACT_CREATE
};

#define FLUSH_ONE 1
#define FLUSH_ALL 2

int setup_map (struct multipath * mpp);
int domap (struct multipath * mpp);
int reinstate_paths (struct multipath *mpp);
int coalesce_paths (struct vectors *vecs, vector curmp, char * refwwid, int force_reload);
char * get_refwwid (char * dev, enum devtypes dev_type, vector pathvec);

