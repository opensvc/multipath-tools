/* some prototypes */
int ux_socket_listen(const char *name);
int send_packet(int fd, const char *buf);
int recv_packet(int fd, char **buf, unsigned int timeout);

#define _MAX_CMD_LEN		512

/*
 * Used for receiving socket command from untrusted socket client where data
 * size is restricted to 512(_MAX_CMD_LEN) at most.
 * Return -EINVAL if data length requested by client exceeded the _MAX_CMD_LEN.
 */
int recv_packet_from_client(int fd, char **buf, unsigned int timeout);
