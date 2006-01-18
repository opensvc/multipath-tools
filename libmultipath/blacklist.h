#ifndef _BLACKLIST_H
#define _BLACKLIST_H

int setup_default_blist (vector);
int alloc_ble_device (vector);
int blacklist (vector, char *);
int blacklist_device (vector, char *, char *);
int blacklist_path (struct config *, struct path *);
int store_ble (vector, char *);
int set_ble_device (vector, char *, char *);
void free_blacklist (vector);
void free_blacklist_device (vector);

#endif /* _BLACKLIST_H */
