#ifndef MAIN_H
#define MAIN_H

#define MAPGCINT 5

enum daemon_status {
	DAEMON_INIT = 0,
	DAEMON_START,
	DAEMON_CONFIGURE,
	DAEMON_IDLE,
	DAEMON_RUNNING,
	DAEMON_SHUTDOWN,
	DAEMON_STATUS_SIZE,
};

enum remove_path_result {
	REMOVE_PATH_FAILURE = 0x0, /* path could not be removed. It is still
				    * part of the kernel map, but its state
				    * is set to INIT_REMOVED, and it will be
				    * removed at the next possible occasion */
	REMOVE_PATH_SUCCESS = 0x1, /* path was removed */
	REMOVE_PATH_DELAY = 0x2, /* path is set to be removed later. it
			          * currently still exists and is part of the
			          * kernel map */
	REMOVE_PATH_MAP_ERROR = 0x5, /* map was removed because of error. value
				      * includes REMOVE_PATH_SUCCESS bit
				      * because the path was also removed */
};

extern pid_t daemon_pid;
extern int uxsock_timeout;

void exit_daemon(void);
const char * daemon_status(void);
enum daemon_status wait_for_state_change_if(enum daemon_status oldstate,
					    unsigned long ms);
void schedule_reconfigure(enum force_reload_types requested_type);
int need_to_delay_reconfig (struct vectors *);
int ev_add_path (struct path *, struct vectors *, int);
int ev_remove_path (struct path *, struct vectors *, int);
int ev_add_map (char *, const char *, struct vectors *);
int ev_remove_map (char *, char *, int, struct vectors *);
int flush_map(struct multipath *, struct vectors *, int);

void handle_signals(bool);
int __setup_multipath (struct vectors * vecs, struct multipath * mpp,
		       int reset);
#define setup_multipath(vecs, mpp) __setup_multipath(vecs, mpp, 1)
int update_multipath (struct vectors *vecs, char *mapname, int reset);
int reload_and_sync_map(struct multipath *mpp, struct vectors *vecs,
			int refresh);

void handle_path_wwid_change(struct path *pp, struct vectors *vecs);
bool check_path_wwid_change(struct path *pp);
int finish_path_init(struct path *pp, struct vectors * vecs);
#endif /* MAIN_H */
