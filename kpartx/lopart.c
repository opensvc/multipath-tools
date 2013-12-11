/* Taken from Ted's losetup.c - Mitch <m.dsouza@mrc-apu.cam.ac.uk> */
/* Added vfs mount options - aeb - 960223 */
/* Removed lomount - aeb - 960224 */

/* 1999-02-22 Arkadiusz Mi¶kiewicz <misiek@pld.ORG.PL>
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
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sysmacros.h>
#include <asm/posix_types.h>
#include <linux/loop.h>

#include "lopart.h"
#include "xstrncpy.h"

#ifndef LOOP_CTL_GET_FREE
#define LOOP_CTL_GET_FREE       0x4C82
#endif

#if !defined (__alpha__) && !defined (__ia64__) && !defined (__x86_64__) \
        && !defined (__s390x__)
#define int2ptr(x)	((void *) ((int) x))
#else
#define int2ptr(x)	((void *) ((long) x))
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

extern int
is_loop_device (const char *device)
{
	struct stat statbuf;
	int loopmajor;
#if 1
	loopmajor = 7;
#else
	FILE *procdev;
	char line[100], *cp;

	loopmajor = 0;

	if ((procdev = fopen(PROC_DEVICES, "r")) != NULL) {
		
		while (fgets (line, sizeof(line), procdev)) {
			
			if ((cp = strstr (line, " loop\n")) != NULL) {
				*cp='\0';
				loopmajor=atoi(line);
				break;
			}
		}

		fclose(procdev);
	}
#endif
	return (loopmajor && stat(device, &statbuf) == 0 &&
		S_ISBLK(statbuf.st_mode) &&
		major(statbuf.st_rdev) == loopmajor);
}

#define SIZE(a) (sizeof(a)/sizeof(a[0]))

extern char *
find_loop_by_file (const char * filename)
{
	char dev[64];
	char *loop_formats[] = { "/dev/loop%d", "/dev/loop/%d" };
	int i, j, fd;
	struct stat statbuf;
	struct loop_info loopinfo;

	for (j = 0; j < SIZE(loop_formats); j++) {

		for (i = 0; i < 256; i++) {
			sprintf (dev, loop_formats[j], i);

			if (stat (dev, &statbuf) != 0 ||
			    !S_ISBLK(statbuf.st_mode))
				continue;

			fd = open (dev, O_RDONLY);

			if (fd < 0)
				break;

			if (ioctl (fd, LOOP_GET_STATUS, &loopinfo) != 0) {
				close (fd);
				continue;
			}

			if (0 == strcmp(filename, loopinfo.lo_name)) {
				close (fd);
				return xstrdup(dev); /*found */
			}

			close (fd);
			continue;
		}
	}
	return NULL;
}

extern char *
find_unused_loop_device (void)
{
	/* Just creating a device, say in /tmp, is probably a bad idea -
	   people might have problems with backup or so.
	   So, we just try /dev/loop[0-7]. */

	char dev[20];
	char *loop_formats[] = { "/dev/loop%d", "/dev/loop/%d" };
	int i, j, fd, first = 0, somedev = 0, someloop = 0, loop_known = 0;
	struct stat statbuf;
	struct loop_info loopinfo;
	FILE *procdev;

	if (stat("/dev/loop-control", &statbuf) == 0 &&
	    S_ISCHR(statbuf.st_mode)) {
		fd = open("/dev/loop-control", O_RDWR);
		if (fd >= 0)
			first = ioctl(fd, LOOP_CTL_GET_FREE);
		close(fd);
		if (first < 0)
			first = 0;
	}
	for (j = 0; j < SIZE(loop_formats); j++) {

	    for(i = first; i < 256; i++) {
		sprintf(dev, loop_formats[j], i);

		if (stat (dev, &statbuf) == 0 && S_ISBLK(statbuf.st_mode)) {
			somedev++;
			fd = open (dev, O_RDONLY);

			if (fd >= 0) {

				if(ioctl (fd, LOOP_GET_STATUS, &loopinfo) == 0)
					someloop++;		/* in use */

				else if (errno == ENXIO) {
					close (fd);
					return xstrdup(dev);/* probably free */
				}

				close (fd);
			}
			
			/* continue trying as long as devices exist */
			continue;
		}
		break;
	    }
	}

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
	
	return 0;
}

extern int
set_loop (const char *device, const char *file, int offset, int *loopro)
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
		perror (device);
		return 1;
	}

	*loopro = (mode == O_RDONLY);
	memset (&loopinfo, 0, sizeof (loopinfo));

	xstrncpy (loopinfo.lo_name, file, LO_NAME_SIZE);
	loopinfo.lo_offset = offset;
	loopinfo.lo_encrypt_type = LO_CRYPT_NONE;
	loopinfo.lo_encrypt_key_size = 0;

	if (ioctl (fd, LOOP_SET_FD, int2ptr(ffd)) < 0) {
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

extern int 
del_loop (const char *device)
{
	int retries = 3;
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
