/*
 * Copyright (c) 2010 Benjamin Marzinski, Redhat
 */

#ifndef FILE_H_INCLUDED
#define FILE_H_INCLUDED

#include <sys/stat.h>

#define FILE_TIMEOUT 30
int ensure_directories_exist(const char *str, mode_t dir_mode);
int open_file(const char *file, int *can_write, const char *header);

#endif /* FILE_H_INCLUDED */
