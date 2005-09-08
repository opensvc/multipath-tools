#ifndef MAIN_H
#define MAIN_H

#define DAEMON 1
#define CHECKINT 5
#define MAPGCINT 5
#define MAX_CHECKINT CHECKINT << 2

struct vectors {
	pthread_mutex_t *lock;
	vector pathvec;
	vector mpvec;
};

int reconfigure (struct vectors *);
int show_paths (char **, int *, struct vectors *);
int show_maps (char **, int *, struct vectors *);
int dump_pathvec (char **, int *, struct vectors *);
int uev_add_path (char *, struct vectors *);
int uev_remove_path (char *, struct vectors *);
int uev_add_map (char *, struct vectors *);
int uev_remove_map (char *, struct vectors *);

#endif /* MAIN_H */
