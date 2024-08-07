/* some prototypes */
#ifndef UXSOCK_H_INCLUDED
#define UXSOCK_H_INCLUDED

int ux_socket_listen(const char *name);
int send_packet(int fd, const char *buf);
int recv_packet(int fd, char **buf, unsigned int timeout);

#define MAX_CMD_LEN		512
#endif
