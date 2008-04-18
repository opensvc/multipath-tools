/*
 * Some code borrowed from sg-utils.
 *
 * Copyright (c) 2004 Christophe Varoqui
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "checkers.h"

#include "../libmultipath/sg_include.h"

#define TUR_CMD_LEN 6
#define HEAVY_CHECK_COUNT       10

#define MSG_TUR_UP	"tur checker reports path is up"
#define MSG_TUR_DOWN	"tur checker reports path is down"

struct tur_checker_context {
	void * dummy;
};

int libcheck_init (struct checker * c)
{
	return 0;
}

void libcheck_free (struct checker * c)
{
	return;
}

extern int
libcheck_check (struct checker * c)
{
	struct sg_io_hdr io_hdr;
        unsigned char turCmdBlk[TUR_CMD_LEN] = { 0x00, 0, 0, 0, 0, 0 };
        unsigned char sense_buffer[32];

        memset(&io_hdr, 0, sizeof (struct sg_io_hdr));
        io_hdr.interface_id = 'S';
        io_hdr.cmd_len = sizeof (turCmdBlk);
        io_hdr.mx_sb_len = sizeof (sense_buffer);
        io_hdr.dxfer_direction = SG_DXFER_NONE;
        io_hdr.cmdp = turCmdBlk;
        io_hdr.sbp = sense_buffer;
        io_hdr.timeout = DEF_TIMEOUT;
        io_hdr.pack_id = 0;
        if (ioctl(c->fd, SG_IO, &io_hdr) < 0) {
		MSG(c, MSG_TUR_DOWN);
                return PATH_DOWN;
        }
        if (io_hdr.info & SG_INFO_OK_MASK) {
		MSG(c, MSG_TUR_DOWN);
                return PATH_DOWN;
        }
	MSG(c, MSG_TUR_UP);
        return PATH_UP;
}
