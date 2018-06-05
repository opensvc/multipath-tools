#include "structs.h"
#include "file.h"
#include "debug.h"
#include "config.h"
#include "util.h"
#include "propsel.h"
#include "prkey.h"
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <libudev.h>
#include <mpath_persist.h>

#define PRKEY_READ 0
#define PRKEY_WRITE 1

static int do_prkey(int fd, char *wwid, char *keystr, int cmd)
{
	char buf[4097];
	char *ptr;
	off_t start = 0;
	int bytes;

	while (1) {
		if (lseek(fd, start, SEEK_SET) < 0) {
			condlog(0, "prkey file read lseek failed : %s",
				strerror(errno));
			return 1;
		}
		bytes = read(fd, buf, 4096);
		if (bytes < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			condlog(0, "failed to read from prkey file : %s",
				strerror(errno));
			return 1;
		}
		if (!bytes) {
			ptr = NULL;
			break;
		}
		buf[bytes] = '\0';
		ptr = strstr(buf, wwid);
		while (ptr) {
			if (ptr == buf || *(ptr - 1) != ' ' ||
			    *(ptr + strlen(wwid)) != '\n')
				ptr = strstr(ptr + strlen(wwid), wwid);
			else
				break;
		}
		if (ptr) {
			condlog(3, "found prkey for '%s'", wwid);
			ptr[strlen(wwid)] = '\0';
			if (ptr - PRKEY_SIZE < buf ||
			    (ptr - PRKEY_SIZE != buf &&
			     *(ptr - PRKEY_SIZE - 1) != '\n')) {
				condlog(0, "malformed prkey file line for wwid: '%s'", ptr);
				return 1;
			}
			ptr = ptr - PRKEY_SIZE;
			break;
		}
		ptr = strrchr(buf, '\n');
		if (ptr == NULL) {
			condlog(4, "couldn't file newline, assuming end of file");
			break;
		}
		start = start + (ptr - buf) + 1;
	}
	if (cmd == PRKEY_READ) {
		if (!ptr || *ptr == '#')
			return 1;
		memcpy(keystr, ptr, PRKEY_SIZE - 1);
		keystr[PRKEY_SIZE - 1] = '\0';
		return 0;
	}
	if (!ptr && !keystr)
		return 0;
	if (ptr) {
		if (lseek(fd, start + (ptr - buf), SEEK_SET) < 0) {
			condlog(0, "prkey write lseek failed : %s",
				strerror(errno));
			return 1;
		}
	}
	if (!keystr) {
		if (safe_write(fd, "#", 1) < 0) {
			condlog(0, "failed to write to prkey file : %s",
				strerror(errno));
			return 1;
		}
		return 0;
	}
	if (!ptr) {
		if (lseek(fd, 0, SEEK_END) < 0) {
			condlog(0, "prkey write lseek failed : %s",
				strerror(errno));
			return 1;
		}
	}
	bytes = sprintf(buf, "%s %s\n", keystr, wwid);
	if (safe_write(fd, buf, bytes) < 0) {
		condlog(0, "failed to write to prkey file: %s",
			strerror(errno));
		return 1;
	}
	return 0;
}

int get_prkey(struct config *conf, struct multipath *mpp, uint64_t *prkey,
	      uint8_t *sa_flags)
{
	int fd;
	int unused;
	int ret = 1;
	char keystr[PRKEY_SIZE];

	if (!strlen(mpp->wwid))
		goto out;

	fd = open_file(conf->prkeys_file, &unused, PRKEYS_FILE_HEADER);
	if (fd < 0)
		goto out;
	ret = do_prkey(fd, mpp->wwid, keystr, PRKEY_READ);
	if (ret)
		goto out_file;
	*sa_flags = 0;
	if (strchr(keystr, 'X'))
		*sa_flags = MPATH_F_APTPL_MASK;
	ret = !!parse_prkey(keystr, prkey);
out_file:
	close(fd);
out:
	return ret;
}

int set_prkey(struct config *conf, struct multipath *mpp, uint64_t prkey,
	      uint8_t sa_flags)
{
	int fd;
	int can_write = 1;
	int ret = 1;
	char keystr[PRKEY_SIZE];

	if (!strlen(mpp->wwid))
		goto out;

	if (sa_flags & ~MPATH_F_APTPL_MASK) {
		condlog(0, "unsupported pr flags, 0x%x",
			sa_flags & ~MPATH_F_APTPL_MASK);
		sa_flags &= MPATH_F_APTPL_MASK;
	}

	fd = open_file(conf->prkeys_file, &can_write, PRKEYS_FILE_HEADER);
	if (fd < 0)
		goto out;
	if (!can_write) {
		condlog(0, "cannot set prkey, prkeys file is read-only");
		goto out_file;
	}
	if (prkey) {
		/* using the capitalization of the 'x' is a hack, but
		 * it's unlikely that mpath_persist will support more options
		 * since sg_persist doesn't, and this lets us keep the
		 * same file format as before instead of needing to change
		 * the format of the prkeys file */
		if (sa_flags)
			snprintf(keystr, PRKEY_SIZE, "0X%016" PRIx64, prkey);
		else
			snprintf(keystr, PRKEY_SIZE, "0x%016" PRIx64, prkey);
		keystr[PRKEY_SIZE - 1] = '\0';
		ret = do_prkey(fd, mpp->wwid, keystr, PRKEY_WRITE);
	}
	else
		ret = do_prkey(fd, mpp->wwid, NULL, PRKEY_WRITE);
	if (ret == 0)
		select_reservation_key(conf, mpp);
	if (get_be64(mpp->reservation_key) != prkey)
		ret = 1;
out_file:
	close(fd);
out:
	return ret;
}
