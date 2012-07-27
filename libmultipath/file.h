/*
 * Copyright (c) 2010 Benjamin Marzinski, Redhat
 */

#ifndef _FILE_H
#define _FILE_H

#define FILE_TIMEOUT 30
int open_file(char *file, int *can_write, char *header);

#endif /* _FILE_H */
