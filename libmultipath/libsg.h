#ifndef _LIBSG_H
#define _LIBSG_H

#define SENSE_BUFF_LEN 32

int sg_read (int sg_fd, unsigned char * buff, int buff_len,
	     unsigned char * sense, int sense_len, unsigned int timeout);

#endif /* _LIBSG_H */
