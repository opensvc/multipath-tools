/* some prototypes */
int ux_socket_listen(const char *name);
int send_packet(int fd, const char *buf);
int recv_packet(int fd, char **buf, unsigned int timeout);
size_t write_all(int fd, const void *buf, size_t len);
ssize_t read_all(int fd, void *buf, size_t len, unsigned int timeout);
