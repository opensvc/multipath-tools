/* Taken from Ted's losetup.c - Mitch <m.dsouza@mrc-apu.cam.ac.uk> */
/* Added vfs mount options - aeb - 960223 */
/* Removed lomount - aeb - 960224 */

/* 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 * Sun Mar 21 1999 - Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 * - fixed strerr(errno) in gettext calls
 */

#define PROC_DEVICES	"/proc/devices"

/*
 * losetup.c - setup and control loop devices
 */

#include "kpartx.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/sysmacros.h>
#include <linux/loop.h>
#include <limits.h>

#include "lopart.h"
#include "xstrncpy.h"

#ifndef LOOP_CTL_GET_FREE
#define LOOP_CTL_GET_FREE       0x4C82
#endif

static char *
xstrdup (const char *s)
{
	char *t;

	if (s == NULL)
		return NULL;

	t = strdup (s);

	if (t == NULL) {
		fprintf(stderr, "not enough memory");
		exit(1);
	}

	return t;
}

#define SIZE(a) (sizeof(a)/sizeof(a[0]))

char *find_loop_by_file(const char *filename)
{
	DIR *dir;
	struct dirent *dent;
	char dev[64], *found = NULL, *p;
	int fd, bytes_read;
	struct stat statbuf;
	struct loop_info loopinfo;
	const char VIRT_BLOCK[] = "/sys/devices/virtual/block";
	char path[PATH_MAX];
	char bf_path[PATH_MAX];
	char backing_file[PATH_MAX];

	dir = opendir(VIRT_BLOCK);
	if (!dir)
		return NULL;

	while ((dent = readdir(dir)) != NULL) {
		if (strncmp(dent->d_name,"loop",4))
			continue;

		if (snprintf(path, PATH_MAX, "%s/%s/dev", VIRT_BLOCK,
			     dent->d_name) >= PATH_MAX)
			continue;

		fd = open(path, O_RDONLY);
		if (fd < 0)
			continue;

		bytes_read = read(fd, dev, sizeof(dev) - 1);
		if (bytes_read <= 0) {
			close(fd);
			continue;
		}

		close(fd);

		dev[bytes_read] = '\0';
		p = strchr(dev, '\n');
		if (p != NULL)
			*p = '\0';
		if (snprintf(path, PATH_MAX, "/dev/block/%s", dev) >= PATH_MAX)
			continue;

		fd = open (path, O_RDONLY);
		if (fd < 0)
			continue;

		if (fstat (fd, &statbuf) != 0 ||
		    !S_ISBLK(statbuf.st_mode)) {
			close (fd);
			continue;
		}

		if (ioctl (fd, LOOP_GET_STATUS, &loopinfo) != 0) {
			close (fd);
			continue;
		}

		close (fd);

		if (0 == strcmp(filename, loopinfo.lo_name)) {
			found = realpath(path, NULL);
			break;
		}

		/*
		 * filename is a realpath, while loopinfo.lo_name may hold just the
		 * basename.  If that's the case, try to match filename against the
		 * backing_file entry for this loop entry
		 */
		if (snprintf(bf_path, PATH_MAX, "%s/%s/loop/backing_file", VIRT_BLOCK,
					 dent->d_name) >= PATH_MAX)
			continue;

		fd = open(bf_path, O_RDONLY);
		if (fd < 0)
			continue;

		bytes_read = read(fd, backing_file, sizeof(backing_file) - 1);
		if (bytes_read <= 0) {
			close(fd);
			continue;
		}

		close(fd);

		backing_file[bytes_read-1] = '\0';

		if (0 == strcmp(filename, backing_file)) {
			found = realpath(path, NULL);
			break;
		}
	}
	closedir(dir);
	return found;
}

char *find_unused_loop_device(void)
{
	char dev[20], *next_loop_dev = NULL;
	int fd, next_loop = 0, somedev = 0, someloop = 0, loop_known = 0;
	struct stat statbuf;
	struct loop_info loopinfo;
	FILE *procdev;

	while (next_loop_dev == NULL) {
		if (stat("/dev/loop-control", &statbuf) == 0 &&
		    S_ISCHR(statbuf.st_mode)) {
			int next_loop_fd;

			next_loop_fd = open("/dev/loop-control", O_RDWR);
			if (next_loop_fd < 0)
				return NULL;
			next_loop = ioctl(next_loop_fd, LOOP_CTL_GET_FREE);
			close(next_loop_fd);
			if (next_loop < 0)
				return NULL;
		}

		sprintf(dev, "/dev/loop%d", next_loop);

		fd = open (dev, O_RDONLY);
		if (fd >= 0) {
			if (fstat (fd, &statbuf) == 0 &&
			    S_ISBLK(statbuf.st_mode)) {
				somedev++;
				if(ioctl (fd, LOOP_GET_STATUS, &loopinfo) == 0)
					someloop++;		/* in use */
				else if (errno == ENXIO)
					next_loop_dev = xstrdup(dev);

			}
			close (fd);

			/* continue trying as long as devices exist */
			continue;
		}
		break;
	}
	if (next_loop_dev)
		return next_loop_dev;

	/* Nothing found. Why not? */
	if ((procdev = fopen(PROC_DEVICES, "r")) != NULL) {
		char line[100];

		while (fgets (line, sizeof(line), procdev))

			if (strstr (line, " loop\n")) {
				loop_known = 1;
				break;
			}

		fclose(procdev);

		if (!loop_known)
			loop_known = -1;
	}

	if (!somedev)
		fprintf(stderr, "mount: could not find any device /dev/loop#");

	else if (!someloop) {
		if (loop_known == 1)
			fprintf(stderr,
				"mount: Could not find any loop device.\n"
				"       Maybe /dev/loop# has a wrong major number?");
		else if (loop_known == -1)
			fprintf(stderr,
				"mount: Could not find any loop device, and, according to %s,\n"
				"       this kernel does not know about the loop device.\n"
				"       (If so, then recompile or `modprobe loop'.)",
				PROC_DEVICES);
		else
			fprintf(stderr,
				"mount: Could not find any loop device. Maybe this kernel does not know\n"
				"       about the loop device (then recompile or `modprobe loop'), or\n"
				"       maybe /dev/loop# has the wrong major number?");
	} else
		fprintf(stderr, "mount: could not find any free loop device");
	return NULL;
}

int set_loop(const char *device, const char *file, int offset, int *loopro)
{
	struct loop_info loopinfo;
	int fd, ffd, mode;

	mode = (*loopro ? O_RDONLY : O_RDWR);

	if ((ffd = open (file, mode)) < 0) {

		if (!*loopro && (errno == EROFS || errno == EACCES))
			ffd = open (file, mode = O_RDONLY);

		if (ffd < 0) {
			perror (file);
			return 1;
		}
	}

	if ((fd = open (device, mode)) < 0) {
		close(ffd);
		perror (device);
		return 1;
	}

	*loopro = (mode == O_RDONLY);
	memset (&loopinfo, 0, sizeof (loopinfo));

	xstrncpy (loopinfo.lo_name, file, LO_NAME_SIZE);
	loopinfo.lo_offset = offset;
	loopinfo.lo_encrypt_type = LO_CRYPT_NONE;
	loopinfo.lo_encrypt_key_size = 0;

	if (ioctl(fd, LOOP_SET_FD, (void*)(uintptr_t)(ffd)) < 0) {
		perror ("ioctl: LOOP_SET_FD");
		close (fd);
		close (ffd);
		return 1;
	}

	if (ioctl (fd, LOOP_SET_STATUS, &loopinfo) < 0) {
		(void) ioctl (fd, LOOP_CLR_FD, 0);
		perror ("ioctl: LOOP_SET_STATUS");
		close (fd);
		close (ffd);
		return 1;
	}

	close (fd);
	close (ffd);
	return 0;
}

int del_loop(const char *device)
{
	int retries = 5;
	int fd;

	if ((fd = open (device, O_RDONLY)) < 0) {
		int errsv = errno;
		fprintf(stderr, "loop: can't delete device %s: %s\n",
			device, strerror (errsv));
		return 1;
	}

	while (ioctl (fd, LOOP_CLR_FD, 0) < 0) {
		if (errno != EBUSY || retries-- <= 0) {
			perror ("ioctl: LOOP_CLR_FD");
			close (fd);
			return 1;
		}
		fprintf(stderr,
			"loop: device %s still in use, retrying delete\n",
			device);
		sleep(1);
		continue;
	}

	close (fd);
	return 0;
}
