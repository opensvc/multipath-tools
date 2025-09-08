#ifndef MPATHPR_H_INCLUDED
#define MPATHPR_H_INCLUDED

/*
 * This header file contains symbols that are only used by
 * libmpathpersist internally.
 */

int update_prflag(char *mapname, int set);
int update_prkey_flags(char *mapname, uint64_t prkey, uint8_t sa_flags);
int get_prflag(char *mapname);
#define update_prkey(mapname, prkey) update_prkey_flags(mapname, prkey, 0)

#endif
