// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Soft:        multipath device mapper target autoconfig
 *
 * Version:     $Id: main.h,v 0.0.1 2003/09/18 15:13:38 cvaroqui Exp $
 *
 * Author:      Christophe Varoqui
 *
 * Copyright (c) 2006 Christophe Varoqui
 */
#ifndef VERSION_H_INCLUDED
#define VERSION_H_INCLUDED

#define VERSION_CODE 0x000D01
/* MMDDYY, in hex */
#define DATE_CODE    0x01141A

#define PROG    "multipath-tools"

#define MULTIPATH_VERSION(version)      \
	(version >> 16) & 0xFF,         \
	(version >> 8) & 0xFF,          \
	version & 0xFF

#define VERSION_STRING PROG" v%d.%d.%d (%.2d/%.2d, 20%.2d)\n",  \
		MULTIPATH_VERSION(VERSION_CODE),                \
		MULTIPATH_VERSION(DATE_CODE)

#endif /* VERSION_H_INCLUDED */
