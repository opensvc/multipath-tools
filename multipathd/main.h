#ifndef MAIN_H
#define MAIN_H

#define MAPGCINT 5

int reconfigure (struct vectors *);
int ev_add_path (char *, struct vectors *);
int ev_remove_path (char *, struct vectors *);
int ev_add_map (char *, char *, struct vectors *);
int ev_remove_map (char *, char *, int, struct vectors *);
void sync_map_state (struct multipath *);

#endif /* MAIN_H */
