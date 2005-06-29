#ifndef MAIN_H
#define MAIN_H

#define DAEMON 1
#define CHECKINT 5
#define MAPGCINT 5
#define MAX_CHECKINT CHECKINT << 2

struct paths {
	pthread_mutex_t *lock;
	vector pathvec;
	vector mpvec;
};

char * show_paths (struct paths *);
char * show_maps (struct paths *);
int uev_add_path (char *, struct paths *);
int uev_remove_path (char *, struct paths *);
int uev_add_map (char *, struct paths *);
int uev_remove_map (char *, struct paths *);

#endif /* MAIN_H */
