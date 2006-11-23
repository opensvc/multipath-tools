#ifndef _LIBSG_H
#define _LIBSG_H

#define SENSE_BUFF_LEN 32

int sg_read (int sg_fd, unsigned char * buff, unsigned char * senseBuff);

#endif /* _LIBSG_H */
