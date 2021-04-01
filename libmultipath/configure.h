/*
 * configurator actions
 */
#define ACT_NOTHING_STR         "unchanged"
#define ACT_REJECT_STR          "reject"
#define ACT_RELOAD_STR          "reload"
#define ACT_SWITCHPG_STR        "switchpg"
#define ACT_RENAME_STR          "rename"
#define ACT_CREATE_STR          "create"
#define ACT_RESIZE_STR          "resize"

enum actions {
	ACT_UNDEF,
	ACT_NOTHING,
	ACT_REJECT,
	ACT_RELOAD,
	ACT_SWITCHPG,
	ACT_RENAME,
	ACT_CREATE,
	ACT_RESIZE,
	ACT_FORCERENAME,
	ACT_DRY_RUN,
	ACT_IMPOSSIBLE,
};

/*
 * Return value of domap()
 * DAEMON_RETRY is only used for ACT_CREATE (see domap()).
 */
enum {
	DOMAP_RETRY	= -1,
	DOMAP_FAIL	= 0,
	DOMAP_OK	= 1,
	DOMAP_EXIST	= 2,
	DOMAP_DRY	= 3
};

/*
 * Return value of coalesce_paths()
 * CP_RETRY is only used in non-daemon case (multipath).
 */
enum {
	CP_OK = 0,
	CP_FAIL,
	CP_RETRY,
};

struct vectors;

int setup_map (struct multipath * mpp, char * params, int params_size,
	       struct vectors *vecs );
void select_action (struct multipath *mpp, const struct _vector *curmp,
		    int force_reload);
int domap (struct multipath * mpp, char * params, int is_daemon);
int reinstate_paths (struct multipath *mpp);
int coalesce_paths (struct vectors *vecs, vector curmp, char * refwwid, int force_reload, enum mpath_cmds cmd);
int get_refwwid (enum mpath_cmds cmd, const char *dev, enum devtypes dev_type,
		 vector pathvec, char **wwid);
struct udev_device *get_udev_device(const char *dev, enum devtypes dev_type);
void trigger_paths_udev_change(struct multipath *mpp, bool is_mpath);
void trigger_partitions_udev_change(struct udev_device *dev, const char *action,
				    int len);
