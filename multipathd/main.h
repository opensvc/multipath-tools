#ifndef MAIN_H
#define MAIN_H

#define MAPGCINT 5

enum daemon_status {
    DAEMON_INIT,
    DAEMON_START,
    DAEMON_CONFIGURE,
    DAEMON_RUNNING,
    DAEMON_SHUTDOWN,
};

int exit_daemon(int);
const char * daemon_status(void);
int reconfigure (struct vectors *);
int ev_add_path (char *, struct vectors *);
int ev_remove_path (char *, struct vectors *);
int ev_add_map (char *, char *, struct vectors *);
int ev_remove_map (char *, char *, int, struct vectors *);
void sync_map_state (struct multipath *);

#endif /* MAIN_H */
