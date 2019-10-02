/*
 * Soft:        multipath device mapper target autoconfig
 *
 * Version:     $Id: main.h,v 0.0.1 2003/09/18 15:13:38 cvaroqui Exp $
 *
 * Author:      Christophe Varoqui
 *
 *              This program is distributed in the hope that it will be useful,
 *              but WITHOUT ANY WARRANTY; without even the implied warranty of
 *              MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *              See the GNU General Public License for more details.
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Copyright (c) 2006 Christophe Varoqui
 */
#ifndef _VERSION_H
#define _VERSION_H

#define VERSION_CODE 0x000803
#define DATE_CODE    0x0a0213

#define PROG    "multipath-tools"

#define MULTIPATH_VERSION(version)      \
	(version >> 16) & 0xFF,         \
	(version >> 8) & 0xFF,          \
	version & 0xFF

#define VERSION_STRING PROG" v%d.%d.%d (%.2d/%.2d, 20%.2d)\n",  \
		MULTIPATH_VERSION(VERSION_CODE),                \
		MULTIPATH_VERSION(DATE_CODE)

#endif /* _VERSION_H */
