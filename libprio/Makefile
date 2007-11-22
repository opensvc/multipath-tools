# Makefile
#
# Copyright (C) 2007 Christophe Varoqui, <christophe.varoqui@free.fr>
#
BUILD = glibc

include ../Makefile.inc

OBJS = libprio.o random.o const.o hp_sw.o emc.o rdac.o alua.o alua_rtpg.o netapp.o hds.o

CFLAGS += -I$(multipathdir)

all: $(BUILD)

prepare:
	@file *-$(BUILD).a >/dev/null 2>&1 || rm -f core *.o *.gz

klibc: prepare $(OBJS)
	ar rs libprio-klibc.a *.o

glibc: prepare $(OBJS)
	ar rs libprio-glibc.a *.o

install:

uninstall:

clean:
	rm -f core *.a *.o *.gz
