/*
 * (C) 2010 Christophe Varoqui
 * (C) 2009 Dembach Goo Infromatik GmbH & Co KG
 * Manon Goo <manon.goo@dg-i.net>
 *
 * datacore.c
 * Version 0.9
 *
 * This program was inspired by work from
 * Matthias Rudolph <matthias.rudolph@hds.com>
 *
 * This work is made available on the basis of the
 * GPLv2 for detials see <http://www.gnu.org/licenses/>.
 *
 * Manon Goo 2009
 *
 *
 */

#include <stdio.h>
#include <sys/ioctl.h>

#include <sys/stat.h>
#include "sg_include.h"
#include "debug.h"
#include "prio.h"
#include "structs.h"

#define INQ_REPLY_LEN 255
#define INQ_CMD_CODE 0x12
#define INQ_CMD_LEN 6

#define dc_log(prio, msg) condlog(prio, "%s: datacore prio: " msg, dev)

int datacore_prio (const char *dev, int sg_fd, char * args)
{
	int k;
	char sdsname[32];
	unsigned char inqCmdBlk[INQ_CMD_LEN] = { INQ_CMD_CODE, 0, 0, 0, INQ_REPLY_LEN, 0 };
	unsigned char inqBuff[INQ_REPLY_LEN];
	unsigned char *inqBuffp = inqBuff;
	unsigned char sense_buffer[32];
	sg_io_hdr_t io_hdr;

	int timeout = 2000;
	char preferredsds_buff[255] = "";
	char * preferredsds = &preferredsds_buff[0];

	if (!args) {
		dc_log(0, "need prio_args with preferredsds set");
		return 0;
	}

	if (sscanf(args, "timeout=%i preferredsds=%s",
		   &timeout, preferredsds) == 2) {}
	else if (sscanf(args, "preferredsds=%s timeout=%i",
			preferredsds, &timeout) == 2) {}
	else if (sscanf(args, "preferredsds=%s",
			preferredsds) == 1) {}
	else {
		dc_log(0, "unexpected prio_args format");
		return 0;
	}

	// on error just return prio 0
	if (strlen(preferredsds) <= 1) {
		dc_log(0, "prio args: preferredsds too short (1 character min)");
		return 0;
	}
	if ((timeout < 500) || (timeout > 20000)) {
		dc_log(0, "prio args: timeout out of bounds [500:20000]");
		return 0;
	}
	if ((ioctl(sg_fd, SG_GET_VERSION_NUM, &k) < 0) || (k < 30000))
		return 0;

	memset (&io_hdr, 0, sizeof (sg_io_hdr_t));
	io_hdr.interface_id = 'S';
	io_hdr.cmd_len = sizeof (inqCmdBlk);
	io_hdr.mx_sb_len = sizeof (sense_buffer);
	io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
	io_hdr.dxfer_len = INQ_REPLY_LEN;
	io_hdr.dxferp = inqBuff;
	io_hdr.cmdp = inqCmdBlk;
	io_hdr.sbp = sense_buffer;
	io_hdr.timeout = timeout;

	// on error just return prio 0
	if (ioctl(sg_fd, SG_IO, &io_hdr) < 0)
		return 0;
	if ((io_hdr.info & SG_INFO_OK_MASK) != SG_INFO_OK)
		return 0;

	snprintf(sdsname, sizeof(sdsname), "%.16s", inqBuffp + 112);

	if (strstr(sdsname , preferredsds))
		return 1;
	return 0;
}

int getprio(struct path * pp, char * args, unsigned int timeout)
{
	return datacore_prio(pp->dev, pp->fd, args);
}
