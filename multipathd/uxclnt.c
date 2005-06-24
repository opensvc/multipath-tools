#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <readline/readline.h>
#include <readline/history.h>

#include <uxsock.h>

/*
 * process the client 
 */
static void process(int fd)
{
	char *line;
	char *reply;

	while ((line = readline("multipathd> "))) {
		size_t len;

		if (send_packet(fd, line, strlen(line) + 1) != 0) break;
		if (recv_packet(fd, &reply, &len) != 0) break;

		printf("%s\n", reply);

		if (line && *line)
			add_history(line);

		free(line);
		free(reply);
	}
}

static void process_req(int fd, char * inbuf)
{
	char *reply;
	size_t len;

	send_packet(fd, inbuf, strlen(inbuf) + 1);
	recv_packet(fd, &reply, &len);

	printf("%s\n", reply);
	free(reply);
}
	
/*
 * entry point
 */
int uxclnt(char * inbuf)
{
	int fd;

	fd = ux_socket_connect(SOCKET_NAME);
	if (fd == -1) {
		perror("ux_socket_connect");
		exit(1);
	}

	if (inbuf)
		process_req(fd, inbuf);
	else
		process(fd);
	
	return 0;
}
