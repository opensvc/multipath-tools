// SPDX-License-Identifier: GPL-2.0-or-later
/*
  efi.[ch] - Manipulates EFI variables as exported in /proc/efi/vars

  Copyright (C) 2001 Dell Computer Corporation <Matt_Domsch@dell.com>
 */

#ifndef EFI_H_INCLUDED
#define EFI_H_INCLUDED

/*
 * Extensible Firmware Interface
 * Based on 'Extensible Firmware Interface Specification'
 *      version 1.02, 12 December, 2000
 */
#include <stdint.h>
#include <string.h>

typedef struct {
	uint8_t  b[16];
} efi_guid_t;

#define EFI_GUID(a,b,c,d0,d1,d2,d3,d4,d5,d6,d7) \
((efi_guid_t) \
{{ (a) & 0xff, ((a) >> 8) & 0xff, ((a) >> 16) & 0xff, ((a) >> 24) & 0xff, \
  (b) & 0xff, ((b) >> 8) & 0xff, \
  (c) & 0xff, ((c) >> 8) & 0xff, \
  (d0), (d1), (d2), (d3), (d4), (d5), (d6), (d7) }})


/******************************************************
 * GUIDs
 ******************************************************/
#define NULL_GUID \
EFI_GUID( 0x00000000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00)

static inline int
efi_guidcmp(efi_guid_t left, efi_guid_t right)
{
	return memcmp(&left, &right, sizeof (efi_guid_t));
}

typedef uint16_t efi_char16_t;		/* UNICODE character */

#endif /* EFI_H_INCLUDED */
