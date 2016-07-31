#ifndef _WEIGHTED_PATH_H
#define _WEIGHTED_PATH_H

#define PRIO_WEIGHTED_PATH "weightedpath"
#define HBTL "hbtl"
#define DEV_NAME "devname"
#define SERIAL "serial"
#define WWN "wwn"
#define DEFAULT_PRIORITY 0

int prio_path_weight(struct path *pp, char *prio_args);

#endif
