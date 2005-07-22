#include <unistd.h>

#include "memory.h"
#include "vector.h"
#include "structs.h"
#include "debug.h"
#include "cache.h"
#include "uxsock.h"

static void
revoke_cache_info(struct path * pp)
{
	pp->checker_context = NULL;
	pp->fd = 0;
	pp->mpp = NULL;
}

int
cache_load (vector pathvec)
{
	char *reply;
	size_t len;
	int fd;
	struct path * pp;
	int r = 1;
	char * p;

        fd = ux_socket_connect(SOCKET_NAME);

	if (fd == -1) {
		condlog(3, "ux_socket_connect error");
		return 1;
	}

	send_packet(fd, "dump pathvec", 13);
	recv_packet(fd, &reply, &len);

	for (p = reply; p < (reply + len); p += sizeof(struct path)) {
		pp = alloc_path();

		if (!pp)
			goto out;

		if (!vector_alloc_slot(pathvec)) {
			free_path(pp);
			goto out;
		}
		vector_set_slot(pathvec, pp);
		memcpy(pp, (void *)p, sizeof(struct path));
		revoke_cache_info(pp);
	}

	r = 0;
out:
	FREE(reply);
	close(fd);
	return r;
}
