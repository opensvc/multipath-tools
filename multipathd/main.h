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

int reconfigure (struct paths *);
int show_paths (char **, int *, struct paths *);
int show_maps (char **, int *, struct paths *);
int dump_pathvec (char **, int *, struct paths * allpaths);
int uev_add_path (char *, struct paths *);
int uev_remove_path (char *, struct paths *);
int uev_add_map (char *, struct paths *);
int uev_remove_map (char *, struct paths *);

#endif /* MAIN_H */
