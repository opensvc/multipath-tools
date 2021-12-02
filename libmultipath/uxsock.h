/* some prototypes */
int ux_socket_listen(const char *name);
int send_packet(int fd, const char *buf);
int recv_packet(int fd, char **buf, unsigned int timeout);

#define _MAX_CMD_LEN		512

