# Makefile
#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@free.fr>
#
BUILD = glibc

include ../Makefile.inc

CFLAGS = -I$(checkersdir)

OBJS = memory.o parser.o vector.o devmapper.o callout.o \
       hwtable.o blacklist.o util.o dmparser.o config.o \
       structs.o discovery.o propsel.o dict.o \
       pgpolicies.o debug.o regex.o defaults.o uevent.o \
       switchgroup.o uxsock.o print.o alias.o log_pthread.o \
       log.o configure.o structs_vec.o

PREVBUILD = $(shell nm debug.o 2> /dev/null|grep log_safe)

ifeq ($(strip $(DAEMON)),1)
	CFLAGS += -DDAEMON
	CLEAN = $(shell if [ "x$(PREVBUILD)" = "x" ]; then echo clean; fi)
else
	CLEAN = $(shell if [ ! "x$(PREVBUILD)" = "x" ]; then echo clean; fi)
endif

all: $(BUILD)

prepare: $(CLEAN)
	@file *-$(BUILD).a >/dev/null 2>&1 || rm -f core *.o *.gz
	@rm -f *-$(BUILD).a

klibc: $(OBJS)
	ar rs libmultipath-klibc.a *.o

glibc: $(OBJS)
	ar rs libmultipath-glibc.a *.o

install:

uninstall:

clean:
	rm -f core *.a *.o *.gz
