/*
 * Christophe Varoqui (2004)
 * This code is GPLv2, see license file
 *
 * This path prioritizer aims to balance logical units over all
 * controllers available. The logic is :
 *
 * - list all paths in all primary path groups
 * - for each path, get the controller's serial
 * - compute the number of active paths attached to each controller
 * - compute the max number of paths attached to the same controller
 * - if sums are already balanced or if the path passed as parameter is
 *   attached to controller with less active paths, then return 
 *   (max_path_attached_to_one_controller - number_of_paths_on_this_controller)
 * - else, or if anything goes wrong, return 1 as a default prio
 *
 */
#define __user

#include <stdio.h>
#include <stdlib.h>
#include <libdevmapper.h>
#include <vector.h>
#include <memory.h>

#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <scsi/sg.h>

#define SERIAL_SIZE 255
#define WORD_SIZE 255
#define PARAMS_SIZE 255
#define FILE_NAME_SIZE 255
#define INQUIRY_CMDLEN  6
#define INQUIRY_CMD     0x12
#define SENSE_BUFF_LEN  32
#define DEF_TIMEOUT     300000
#define RECOVERED_ERROR 0x01
#define MX_ALLOC_LEN    255
#define SCSI_CHECK_CONDITION    0x2
#define SCSI_COMMAND_TERMINATED 0x22
#define SG_ERR_DRIVER_SENSE     0x08

#if DEBUG
#define debug(format, arg...) fprintf(stderr, format "\n", ##arg)
#else
#define debug(format, arg...) do {} while(0)
#endif

#define safe_sprintf(var, format, args...)	\
	snprintf(var, sizeof(var), format, ##args) >= sizeof(var)
#define safe_snprintf(var, size, format, args...)      \
	snprintf(var, size, format, ##args) >= size

struct path {
	char dev_t[WORD_SIZE];
	char serial[SERIAL_SIZE];
};

struct controller {
	char serial[SERIAL_SIZE];
	int path_count;
};

static int
exit_tool (int ret)
{
	printf("1\n");
	exit(ret);
}

static int
opennode (char * devt, int mode)
{
	char devpath[FILE_NAME_SIZE];
	unsigned int major;
	unsigned int minor;
	int fd;

	sscanf(devt, "%u:%u", &major, &minor);
	memset(devpath, 0, FILE_NAME_SIZE);
	
	if (safe_sprintf(devpath, "/tmp/.pp_balance.%u.%u.devnode",
			 major, minor)) {
		fprintf(stderr, "devpath too small\n");
		return -1;
	}
	unlink (devpath);
	mknod(devpath, S_IFBLK|S_IRUSR|S_IWUSR, makedev(major, minor));
	fd = open(devpath, mode);
	
	if (fd < 0)
		unlink(devpath);

	return fd;

}

static void
closenode (char * devt, int fd)
{
	char devpath[FILE_NAME_SIZE];
	unsigned int major;
	unsigned int minor;

	if (fd >= 0)
		close(fd);

	sscanf(devt, "%u:%u", &major, &minor);
	if (safe_sprintf(devpath, "/tmp/.pp_balance.%u.%u.devnode",
			 major, minor)) {
		fprintf(stderr, "devpath too small\n");
		return;
	}
	unlink(devpath);
}

static int
do_inq(int sg_fd, int cmddt, int evpd, unsigned int pg_op,
       void *resp, int mx_resp_len, int noisy)
{
        unsigned char inqCmdBlk[INQUIRY_CMDLEN] =
            { INQUIRY_CMD, 0, 0, 0, 0, 0 };
        unsigned char sense_b[SENSE_BUFF_LEN];
        struct sg_io_hdr io_hdr;
                                                                                                                 
        if (cmddt)
                inqCmdBlk[1] |= 2;
        if (evpd)
                inqCmdBlk[1] |= 1;
        inqCmdBlk[2] = (unsigned char) pg_op;
	inqCmdBlk[3] = (unsigned char)((mx_resp_len >> 8) & 0xff);
	inqCmdBlk[4] = (unsigned char) (mx_resp_len & 0xff);
        memset(&io_hdr, 0, sizeof (struct sg_io_hdr));
        io_hdr.interface_id = 'S';
        io_hdr.cmd_len = sizeof (inqCmdBlk);
        io_hdr.mx_sb_len = sizeof (sense_b);
        io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
        io_hdr.dxfer_len = mx_resp_len;
        io_hdr.dxferp = resp;
        io_hdr.cmdp = inqCmdBlk;
        io_hdr.sbp = sense_b;
        io_hdr.timeout = DEF_TIMEOUT;
 
        if (ioctl(sg_fd, SG_IO, &io_hdr) < 0)
                return -1;
 
        /* treat SG_ERR here to get rid of sg_err.[ch] */
        io_hdr.status &= 0x7e;
        if ((0 == io_hdr.status) && (0 == io_hdr.host_status) &&
            (0 == io_hdr.driver_status))
                return 0;
        if ((SCSI_CHECK_CONDITION == io_hdr.status) ||
            (SCSI_COMMAND_TERMINATED == io_hdr.status) ||
            (SG_ERR_DRIVER_SENSE == (0xf & io_hdr.driver_status))) {
                if (io_hdr.sbp && (io_hdr.sb_len_wr > 2)) {
                        int sense_key;
                        unsigned char * sense_buffer = io_hdr.sbp;
                        if (sense_buffer[0] & 0x2)
                                sense_key = sense_buffer[1] & 0xf;
                        else
                                sense_key = sense_buffer[2] & 0xf;
                        if(RECOVERED_ERROR == sense_key)
                                return 0;
                }
        }
        return -1;
}

static int
get_serial (char * str, int maxlen, char * devt)
{
	int fd;
        int len;
        char buff[MX_ALLOC_LEN + 1];

	fd = opennode(devt, O_RDONLY);

	if (fd < 0)
                return 1;

	if (0 == do_inq(fd, 0, 1, 0x80, buff, MX_ALLOC_LEN, 0)) {
		len = buff[3];
		if (len >= maxlen)
			return 1;
		if (len > 0) {
			memcpy(str, buff + 4, len);
			buff[len] = '\0';
		}
		close(fd);
		return 0;
	}

	closenode(devt, fd);
        return 1;
}

static void *
get_params (void)
{
	struct dm_task *dmt, *dmt1;
	struct dm_names *names = NULL;
	unsigned next = 0;
	void *nexttgt;
	uint64_t start, length;
	char *target_type = NULL;
	char *params;
	char *pp;
	vector paramsvec = NULL;

	if (!(dmt = dm_task_create(DM_DEVICE_LIST)))
		return NULL;

	if (!dm_task_run(dmt))
		goto out;

	if (!(names = dm_task_get_names(dmt)))
		goto out;

	if (!names->dev) {
		debug("no devmap found");
		goto out;
	}
	do {
		/*
		 * keep only multipath maps
		 */
		names = (void *) names + next;
		nexttgt = NULL;
		debug("devmap %s :", names->name);

		if (!(dmt1 = dm_task_create(DM_DEVICE_TABLE)))
			goto out;

		if (!dm_task_set_name(dmt1, names->name))
			goto out1;

		if (!dm_task_run(dmt1))
			goto out1;

		do {
			nexttgt = dm_get_next_target(dmt1, nexttgt,
						     &start,
						     &length,
						     &target_type,
						     &params);
			debug("\\_ %lu %lu %s", (unsigned long) start,
				(unsigned long) length,
				target_type);

			if (!target_type) {
				debug("unknown target type");
				goto out1;
			}

			if (!strncmp(target_type, "multipath", 9)) {
				if (!paramsvec)
					paramsvec = vector_alloc();

				pp = malloc(PARAMS_SIZE);
				strncpy(pp, params, PARAMS_SIZE);
				vector_alloc_slot(paramsvec);
				vector_set_slot(paramsvec, pp);
			} else
				debug("skip non multipath target");
		} while (nexttgt);
out1:
		dm_task_destroy(dmt1);
		next = names->next;
	} while (next);
out:
	dm_task_destroy(dmt);
	return paramsvec;
}

static int
get_word (char *sentence, char *word)
{
	char *p;
	int skip = 0;
	
	while (*sentence ==  ' ') {
		sentence++;
		skip++;
	}
	p = sentence;

	while (*p !=  ' ' && *p != '\0')
		p++;

	skip += (p - sentence);

	if (p - sentence > WORD_SIZE) {
		fprintf(stderr, "word too small\n");
		exit_tool(1);
	}
	strncpy(word, sentence, WORD_SIZE);
	word += p - sentence;
	*word = '\0';

	if (*p == '\0')
		return 0;

	return skip;
}

static int
is_path (char * word)
{
	char *p;

	if (!word)
		return 0;

	p = word;

	while (*p != '\0') {
		if (*p == ':')
			return 1;
		p++;
	}
	return 0;
}

static int
get_paths (vector pathvec)
{
	vector paramsvec = NULL;
	char * str;
	struct path * pp;
	int i;
	enum where {BEFOREPG, INPG, AFTERPG};
	int pos = BEFOREPG;

	if (!pathvec)
		return 1;

	if (!(paramsvec = get_params()))
		exit_tool(0);

	vector_foreach_slot (paramsvec, str, i) {
		debug("params %s", str);
		while (pos != AFTERPG) {
			pp = zalloc(sizeof(struct path));
			str += get_word(str, pp->dev_t);

			if (!is_path(pp->dev_t)) {
				debug("skip \"%s\"", pp->dev_t);
				free(pp);

				if (pos == INPG)
					pos = AFTERPG;
				
				continue;
			}
			if (pos == BEFOREPG)
				pos = INPG;

			get_serial(pp->serial, SERIAL_SIZE, pp->dev_t);
			vector_alloc_slot(pathvec);
			vector_set_slot(pathvec, pp);
			debug("store %s [%s]",
				pp->dev_t, pp->serial);
		}
		pos = BEFOREPG;
	}
	return 0;
}

static void *
find_controller (vector controllers, char * serial)
{
	int i;
	struct controller * cp;

	if (!controllers)
		return NULL;

	vector_foreach_slot (controllers, cp, i)
		if (!strncmp(cp->serial, serial, SERIAL_SIZE))
				return cp;
	return NULL;
}

static void
get_controllers (vector controllers, vector pathvec)
{
	int i;
	struct path * pp;
	struct controller * cp;
	
	if (!controllers)
		return;

	vector_foreach_slot (pathvec, pp, i) {
		if (!pp || !strlen(pp->serial))
			continue;
		
		cp = find_controller(controllers, pp->serial);

		if (!cp) {
			cp = zalloc(sizeof(struct controller));
			vector_alloc_slot(controllers);
			vector_set_slot(controllers, cp);
			strncpy(cp->serial, pp->serial, SERIAL_SIZE);
		}
		cp->path_count++;	
	}
}

static int
get_max_path_count (vector controllers)
{
	int i;
	int max = 0;
	struct controller * cp;

	if (!controllers)
		return 0;

	vector_foreach_slot (controllers, cp, i) {
		debug("controller %s : %i paths", cp->serial, cp->path_count);
		if(cp->path_count > max)
			max = cp->path_count;
	}
	debug("max_path_count = %i", max);
	return max;
}

int
main (int argc, char **argv)
{
	vector pathvec = NULL;
	vector controllers = NULL;
	struct path * ref_path = NULL;
	struct controller * cp = NULL;
	int max_path_count = 0;

	ref_path = zalloc(sizeof(struct path));

	if (!ref_path)
		exit_tool(1);

	if (argc != 2)
		exit_tool(1);

	if (optind<argc)
		strncpy(ref_path->dev_t, argv[optind], WORD_SIZE);

	get_serial(ref_path->serial, SERIAL_SIZE, ref_path->dev_t);

	if (!ref_path->serial || !strlen(ref_path->serial))
		exit_tool(0);

	pathvec = vector_alloc();
	controllers = vector_alloc();

	get_paths(pathvec);
	get_controllers(controllers, pathvec);
	max_path_count = get_max_path_count(controllers);
	cp = find_controller(controllers, ref_path->serial);

	if (!cp) {
		debug("no other active path on serial %s\n",
			ref_path->serial);
		exit_tool(0);
	}

	printf("%i\n", max_path_count - cp->path_count + 1);

	return(0);
}
