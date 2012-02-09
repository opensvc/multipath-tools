# Makefile.inc
#
# Copyright (C) 2004 Christophe Varoqui, <christophe.varoqui@opensvc.com>

#
# Allow to force some libraries to be used statically. (Uncomment one of the
# following lines or define the values when calling make.)
#
# WITH_LOCAL_LIBDM	= 1
# WITH_LOCAL_LIBSYSFS	= 1

ifeq ($(TOPDIR),)
	TOPDIR	= ..
endif

ifndef LIB
	ifeq ($(shell test -d /lib64 && echo 1),1)
		LIB=lib64
	else
		LIB=lib
	endif
endif

prefix      = 
exec_prefix = $(prefix)
bindir      = $(exec_prefix)/sbin
libudevdir  = ${prefix}/lib/udev
multipathdir = $(TOPDIR)/libmultipath
mandir      = $(prefix)/usr/share/man/man8
man5dir     = $(prefix)/usr/share/man/man5
man3dir      = $(prefix)/usr/share/man/man3
rcdir	    = $(prefix)/etc/init.d
syslibdir   = $(prefix)/$(LIB)
libdir	    = $(prefix)/$(LIB)/multipath
unitdir     = $(prefix)/lib/systemd/system
mpathpersistdir = $(TOPDIR)/libmpathpersist

GZIP        = /bin/gzip -9 -c
INSTALL_PROGRAM = install

OPTFLAGS     = -pipe -g -Wall -Wunused -Wstrict-prototypes
CFLAGS	     = $(OPTFLAGS) -fPIC -DLIB_STRING=\"${LIB}\"
SHARED_FLAGS = -shared

%.o:	%.c
	$(CC) $(CFLAGS) -c -o $@ $<

