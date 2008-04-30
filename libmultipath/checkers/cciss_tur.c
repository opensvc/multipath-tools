/*
 *****************************************************************************
 *                                                                           *
 *     (C)  Copyright 2007 Hewlett-Packard Development Company, L.P          *
 *                                                                           *
 * This program is free software; you can redistribute it and/or modify it   *
 * under the terms of the GNU General Public License as published by the Free*
 * Software  Foundation; either version 2 of the License, or (at your option)*
 * any later version.                                                        *
 *                                                                           *
 * This program is distributed in the hope that it will be useful, but       *
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY*
 * or FITNESS FOR  A PARTICULAR PURPOSE. See the GNU General Public License  *
 * for more details.                                                         *
 *                                                                           *
 * You should have received a copy of the GNU General Public License along   *
 * with this program; if not, write to the Free Software Foundation, Inc.,   *
 * 675 Mass Ave, Cambridge, MA 02139, USA.                                   *
 *                                                                           *
 * The copy of the GNU General Public License is available at                *
 * /opt/hp/HPDMmultipath-tool directoy                                       *
 *                                                                           *
 *****************************************************************************
*/

/*
 *  This program originally derived from and inspired by
 *  Christophe Varoqui's tur.c, part of libchecker.
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

#include "cciss.h"

#define TUR_CMD_LEN 6
#define HEAVY_CHECK_COUNT       10

#define MSG_CCISS_TUR_UP	"cciss_tur checker reports path is up"
#define MSG_CCISS_TUR_DOWN	"cciss_tur checker reports path is down"

struct cciss_tur_checker_context {
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
	int rc;
	int ret;
	unsigned int lun = 0;
	struct cciss_tur_checker_context * ctxt = NULL;
	LogvolInfo_struct    lvi;       // logical "volume" info
	IOCTL_Command_struct cic;       // cciss ioctl command

	if ((c->fd) <= 0) {
		MSG(c,"no usable fd");
		ret = -1;
		goto out;
	}

	rc = ioctl(c->fd, CCISS_GETLUNINFO, &lvi);
	if ( rc != 0) {
		perror("Error: ");
		fprintf(stderr, "cciss TUR  failed in CCISS_GETLUNINFO: %s\n",
			strerror(errno));
		MSG(c,MSG_CCISS_TUR_DOWN);
		ret = PATH_DOWN;
		goto out;
	} else {
		lun = lvi.LunID;
	}

	memset(&cic, 0, sizeof(cic));
	cic.LUN_info.LogDev.VolId = lun & 0x3FFFFFFF;
	cic.LUN_info.LogDev.Mode = 0x01; /* logical volume addressing */
	cic.Request.CDBLen = 6;  /* need to try just 2 bytes here */
	cic.Request.Type.Type =  TYPE_CMD; // It is a command.
	cic.Request.Type.Attribute = ATTR_SIMPLE;
	cic.Request.Type.Direction = XFER_NONE;
	cic.Request.Timeout = 0;

	cic.Request.CDB[0] = 0;
	cic.Request.CDB[1] = 0;
	cic.Request.CDB[2] = 0;
	cic.Request.CDB[3] = 0;
	cic.Request.CDB[4] = 0;
	cic.Request.CDB[5] = 0;

	rc = ioctl(c->fd, CCISS_PASSTHRU, &cic);
	if (rc < 0) {
		fprintf(stderr, "cciss TUR  failed: %s\n",
			strerror(errno));
		MSG(c,MSG_CCISS_TUR_DOWN);
		ret = PATH_DOWN;
		goto out;
	}

	if ((cic.error_info.CommandStatus | cic.error_info.ScsiStatus )) {
		MSG(c,MSG_CCISS_TUR_DOWN);
		ret = PATH_DOWN;
		goto out;
	}

	MSG(c,MSG_CCISS_TUR_UP);

	ret = PATH_UP;
out:
	/*
	 * caller told us he doesn't want to keep the context :
	 * free it
	 */
	if (!c->context)
		free(ctxt);

	return(ret);
}
