# Makefile.inc
#
# Copyright (C) 2004 Christophe Varoqui, <christophe.varoqui@free.fr>

#
# Allow to force some libraries to be used statically. (Uncomment one of the
# following lines or define the values when calling make.)
#
# WITH_LOCAL_LIBDM	= 1
# WITH_LOCAL_LIBSYSFS	= 1

ifeq ($(TOPDIR),)
	TOPDIR	= ..
endif

ifeq ($(strip $(BUILD)),klibc)
	CC = klcc
	klibcdir = /usr/lib/klibc
	libdm    = $(klibcdir)/lib/libdevmapper.a
endif

prefix      = 
exec_prefix = $(prefix)
bindir      = $(exec_prefix)/sbin
libudevdir  = ${prefix}/lib/udev
checkersdir = $(TOPDIR)/libcheckers
libpriodir  = $(TOPDIR)/libprio
multipathdir = $(TOPDIR)/libmultipath
mandir      = $(prefix)/usr/share/man/man8
man5dir     = $(prefix)/usr/share/man/man5
rcdir	    = $(prefix)/etc/init.d

GZIP        = /bin/gzip -9 -c

CHECKERSLIB = $(checkersdir)/libcheckers
MULTIPATHLIB = $(multipathdir)/libmultipath
LIBPRIO = $(libpriodir)/libprio

INSTALL_PROGRAM = install -s

OPTFLAGS     = -pipe -g -Wall -Wunused -Wstrict-prototypes
CFLAGS	     = $(OPTFLAGS)

%.o:	%.c
	$(CC) $(CFLAGS) -c -o $@ $<

