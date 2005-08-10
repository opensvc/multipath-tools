#ifndef _BLACKLIST_H
#define _BLACKLIST_H

#define BLIST_ENTRY_SIZE 255

int setup_default_blist (vector blist);
int blacklist (vector blist, char * dev);
int store_regex (vector blist, char * regex);
void free_blacklist (vector blist);

#endif /* _BLACKLIST_H */
