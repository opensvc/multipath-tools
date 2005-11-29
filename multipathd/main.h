#ifndef MAIN_H
#define MAIN_H

#define DAEMON 1
#define CHECKINT 5
#define MAPGCINT 5
#define MAX_CHECKINT CHECKINT << 2

int reconfigure (struct vectors *);
int show_paths (char **, int *, struct vectors *, char *);
int show_maps (char **, int *, struct vectors *, char *);
int ev_add_path (char *, struct vectors *);
int ev_remove_path (char *, struct vectors *);
int ev_add_map (char *, struct vectors *);
int ev_remove_map (char *, struct vectors *);

#endif /* MAIN_H */
