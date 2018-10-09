/*
 * (C) Copyright HUAWEI Technology Corp. 2017, All Rights Reserved.
 *
 * path_latency.c
 *
 * Prioritizer for device mapper multipath, where the corresponding priority
 * values of specific paths are provided by a latency algorithm. And the
 * latency algorithm is dependent on arguments("io_num" and "base_num").
 *
 * The principle of the algorithm as follows:
 * 1. By sending a certain number "io_num" of read IOs to the current path
 *    continuously, the IOs' average latency can be calculated.
 * 2. Max value and min value of average latency are constant. According to
 *    the average latency of each path and the "base_num" of logarithmic
 *    scale, the priority "rc" of each path can be provided.
 *
 * Author(s): Yang Feng <philip.yang@huawei.com>
 * Revised:   Guan Junxiong <guanjunxiong@huawei.com>
 *
 * This file is released under the GPL version 2, or any later version.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <unistd.h>

#include "debug.h"
#include "prio.h"
#include "structs.h"
#include "util.h"
#include "time-util.h"

#define pp_pl_log(prio, fmt, args...) condlog(prio, "path_latency prio: " fmt, ##args)

#define MAX_IO_NUM		200
#define MIN_IO_NUM		20
#define DEF_IO_NUM		100

#define MAX_BASE_NUM		10
#define MIN_BASE_NUM		1.1
// This is 10**(1/4). 4 prio steps correspond to a factor of 10.
#define DEF_BASE_NUM		1.77827941004

#define MAX_AVG_LATENCY		100000000.	/* Unit: us */
#define MIN_AVG_LATENCY		1.		/* Unit: us */

#define DEFAULT_PRIORITY	0

#define USEC_PER_SEC		1000000LL
#define NSEC_PER_USEC		1000LL

#define DEF_BLK_SIZE		4096

static int prepare_directio_read(int fd, int *blksz, char **pbuf,
		int *restore_flags)
{
	unsigned long pgsize = getpagesize();
	long flags;

	if (ioctl(fd, BLKBSZGET, blksz) < 0) {
		pp_pl_log(3,"catnnot get blocksize, set default");
		*blksz = DEF_BLK_SIZE;
	}
	if (posix_memalign((void **)pbuf, pgsize, *blksz))
		return -1;

	flags = fcntl(fd, F_GETFL);
	if (flags < 0)
		goto free_out;
	if (!(flags & O_DIRECT)) {
		flags |= O_DIRECT;
		if (fcntl(fd, F_SETFL, flags) < 0)
			goto free_out;
		*restore_flags = 1;
	}

	return 0;

free_out:
	free(*pbuf);

	return -1;
}

static void cleanup_directio_read(int fd, char *buf, int restore_flags)
{
	long flags;

	free(buf);

	if (!restore_flags)
		return;
	if ((flags = fcntl(fd, F_GETFL)) >= 0) {
		int ret __attribute__ ((unused));
		flags &= ~O_DIRECT;
		/* No point in checking for errors */
		ret = fcntl(fd, F_SETFL, flags);
	}
}

static int do_directio_read(int fd, unsigned int timeout, char *buf, int sz)
{
	fd_set read_fds;
	struct timeval tm = { .tv_sec = timeout };
	int ret;
	int num_read;

	if (lseek(fd, 0, SEEK_SET) == -1)
		return -1;
	FD_ZERO(&read_fds);
	FD_SET(fd, &read_fds);
	ret = select(fd+1, &read_fds, NULL, NULL, &tm);
	if (ret <= 0)
		return -1;
	num_read = read(fd, buf, sz);
	if (num_read != sz)
		return -1;

	return 0;
}

int check_args_valid(int io_num, double base_num)
{
	if ((io_num < MIN_IO_NUM) || (io_num > MAX_IO_NUM)) {
		pp_pl_log(0, "args io_num is outside the valid range");
		return 0;
	}

	if ((base_num < MIN_BASE_NUM) || (base_num > MAX_BASE_NUM)) {
		pp_pl_log(0, "args base_num is outside the valid range");
		return 0;
	}

	return 1;
}

/*
 * In multipath.conf, args form: io_num=n base_num=m. For example, args are
 * "io_num=20 base_num=10", this function can get io_num value 20 and
 * base_num value 10.
 */
static int get_ionum_and_basenum(char *args, int *ionum, double *basenum)
{
	char split_char[] = " \t";
	char *arg, *temp;
	char *str, *str_inval;
	int i;
	int flag_io = 0, flag_base = 0;

	if ((args == NULL) || (ionum == NULL) || (basenum == NULL)) {
		pp_pl_log(0, "args string is NULL");
		return 0;
	}

	arg = temp = STRDUP(args);
	if (!arg)
		return 0;

	for (i = 0; i < 2; i++) {
		str = get_next_string(&temp, split_char);
		if (!str)
			goto out;
		if (!strncmp(str, "io_num=", 7) && strlen(str) > 7) {
			*ionum = (int)strtoul(str + 7, &str_inval, 10);
			if (str == str_inval)
				goto out;
			flag_io = 1;
		}
		else if (!strncmp(str, "base_num=", 9) && strlen(str) > 9) {
			*basenum = strtod(str + 9, &str_inval);
			if (str == str_inval)
				goto out;
			flag_base = 1;
		}
	}

	if (!flag_io || !flag_base)
		goto out;
	if (check_args_valid(*ionum, *basenum) == 0)
		goto out;

	FREE(arg);
	return 1;
out:
	FREE(arg);
	return 0;
}

/*
 * Do not scale the prioriy in a certain range such as [0, 1024]
 * because scaling will eliminate the effect of base_num.
 */
int calcPrio(double lg_avglatency, double lg_maxavglatency,
		double lg_minavglatency)
{
	if (lg_avglatency <= lg_minavglatency)
		return lg_maxavglatency - lg_minavglatency;

	if (lg_avglatency >= lg_maxavglatency)
		return 0;

	return lg_maxavglatency - lg_avglatency;
}

int getprio(struct path *pp, char *args, unsigned int timeout)
{
	int rc, temp;
	int io_num = 0;
	double base_num = 0;
	double lg_avglatency, lg_maxavglatency, lg_minavglatency;
	double standard_deviation;
	double lg_toldelay = 0;
	int blksize;
	char *buf;
	int restore_flags = 0;
	double lg_base;
	double sum_squares = 0;

	if (pp->fd < 0)
		return -1;

	if (get_ionum_and_basenum(args, &io_num, &base_num) == 0) {
		io_num = DEF_IO_NUM;
		base_num = DEF_BASE_NUM;
		pp_pl_log(0, "%s: fails to get path_latency args, set default:"
				"io_num=%d base_num=%.3lf",
				pp->dev, io_num, base_num);
	}

	lg_base = log(base_num);
	lg_maxavglatency = log(MAX_AVG_LATENCY) / lg_base;
	lg_minavglatency = log(MIN_AVG_LATENCY) / lg_base;

	if (prepare_directio_read(pp->fd, &blksize, &buf, &restore_flags) < 0)
		return PRIO_UNDEF;

	temp = io_num;
	while (temp-- > 0) {
		struct timespec tv_before, tv_after, tv_diff;
		double diff, reldiff;

		(void)clock_gettime(CLOCK_MONOTONIC, &tv_before);

		if (do_directio_read(pp->fd, timeout, buf, blksize)) {
			pp_pl_log(0, "%s: path down", pp->dev);
			cleanup_directio_read(pp->fd, buf, restore_flags);
			return -1;
		}

		(void)clock_gettime(CLOCK_MONOTONIC, &tv_after);

		timespecsub(&tv_after, &tv_before, &tv_diff);
		diff = tv_diff.tv_sec * 1000 * 1000 + tv_diff.tv_nsec / 1000;

		if (diff == 0)
			/*
			 * Avoid taking log(0).
			 * This unlikely case is treated as minimum -
			 * the sums don't increase
			 */
			continue;

		/* we scale by lg_base here */
		reldiff = log(diff) / lg_base;

		/*
		 * We assume that the latency complies with Log-normal
		 * distribution. The logarithm of latency is in normal
		 * distribution.
		 */
		lg_toldelay += reldiff;
		sum_squares += reldiff * reldiff;
	}

	cleanup_directio_read(pp->fd, buf, restore_flags);

	lg_avglatency = lg_toldelay / (long long)io_num;

	if (lg_avglatency > lg_maxavglatency) {
		pp_pl_log(2,
			  "%s: average latency (%lld us) is outside the thresold (%lld us)",
			  pp->dev, (long long)pow(base_num, lg_avglatency),
			  (long long)MAX_AVG_LATENCY);
		return DEFAULT_PRIORITY;
	}

	standard_deviation = sqrt((sum_squares - lg_toldelay * lg_avglatency)
				  / (io_num - 1));

	rc = calcPrio(lg_avglatency, lg_maxavglatency, lg_minavglatency);

	pp_pl_log(3, "%s: latency avg=%.2e uncertainty=%.1f prio=%d\n",
		  pp->dev, exp(lg_avglatency * lg_base),
		  exp(standard_deviation * lg_base), rc);

	return rc;
}
