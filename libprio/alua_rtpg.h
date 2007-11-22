/*
 * (C) Copyright IBM Corp. 2004, 2005   All Rights Reserved.
 *
 * rtpg.h
 *
 * Tool to make use of a SCSI-feature called Asymmetric Logical Unit Access.
 * It determines the ALUA state of a device and prints a priority value to
 * stdout.
 *
 * Author(s): Jan Kunigk
 *            S. Bader <shbader@de.ibm.com>
 * 
 * This file is released under the GPL.
 */
#ifndef __RTPG_H__
#define __RTPG_H__
#include "alua_spc3.h"

#define RTPG_SUCCESS				0
#define RTPG_INQUIRY_FAILED			1
#define RTPG_NO_TPG_IDENTIFIER			2
#define RTPG_RTPG_FAILED			3
#define RTPG_TPG_NOT_FOUND			4

int get_target_port_group_support(int fd);
int get_target_port_group(int fd);
int get_asymmetric_access_state(int fd, unsigned int tpg);

#endif /* __RTPG_H__ */

