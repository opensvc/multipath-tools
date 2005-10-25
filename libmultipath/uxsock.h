/* some prototypes */
int ux_socket_connect(const char *name);
int ux_socket_listen(const char *name);
int send_packet(int fd, const char *buf, size_t len);
int recv_packet(int fd, char **buf, size_t *len);
size_t write_all(int fd, const void *buf, size_t len);
size_t read_all(int fd, void *buf, size_t len);
