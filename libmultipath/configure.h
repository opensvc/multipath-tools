/*
 * configurator actions
 */
#define ACT_NOTHING_STR         "unchanged"
#define ACT_RELOAD_STR          "reload"
#define ACT_SWITCHPG_STR        "switchpg"
#define ACT_CREATE_STR          "create"

enum actions {
	ACT_UNDEF,
	ACT_NOTHING,
	ACT_REJECT,
	ACT_RELOAD,
	ACT_SWITCHPG,
	ACT_CREATE
};

#define FLUSH_ONE 1
#define FLUSH_ALL 2

int reinstate_paths (struct multipath *mpp);
int coalesce_paths (struct vectors *vecs, vector curmp);
