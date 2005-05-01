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
	libsysfs = $(klibcdir)/lib/libsysfs.a
endif

prefix      = 
exec_prefix = $(prefix)
bindir      = $(exec_prefix)/sbin
checkersdir = $(TOPDIR)/libcheckers
multipathdir = $(TOPDIR)/libmultipath
mandir      = /usr/share/man/man8

GZIP        = /bin/gzip -9 -c
STRIP       = strip --strip-all -R .comment -R .note

CHECKERSLIB = $(checkersdir)/libcheckers
MULTIPATHLIB = $(multipathdir)/libmultipath
