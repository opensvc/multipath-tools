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

#include <uxsock.h>

/*
 * process the client 
 */
static void process(int fd)
{
	char line[1000];
	char *reply;

	while (fgets(line, sizeof(line), stdin)) {
		size_t len = strlen(line);

		if (line[len-1] == '\n') {
			line[len-1] = 0;
			len--;
		}
		
		if (send_packet(fd, line, strlen(line)) != 0) break;
		if (recv_packet(fd, &reply, &len) != 0) break;

		printf("%*.*s\n", (int)len, (int)len, reply);
		free(reply);
	}
}

static void process_req(int fd, char * inbuf)
{
	char *reply;
	size_t len;

	send_packet(fd, inbuf, strlen(inbuf));
	recv_packet(fd, &reply, &len);

	printf("%*.*s\n", (int)len, (int)len, reply);
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
