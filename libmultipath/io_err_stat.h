#ifndef IO_ERR_STAT_H_INCLUDED
#define IO_ERR_STAT_H_INCLUDED

#include "vector.h"
#include "lock.h"


extern pthread_attr_t io_err_stat_attr;

int start_io_err_stat_thread(void *data);
void stop_io_err_stat_thread(void);
int io_err_stat_handle_pathfail(struct path *path);
int need_io_err_check(struct path *pp);

#endif /* IO_ERR_STAT_H_INCLUDED */
