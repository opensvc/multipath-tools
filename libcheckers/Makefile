# Makefile
#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@free.fr>
#
BUILD = glibc

include ../Makefile.inc

OBJS = checkers.o readsector0.o tur.o directio.o emc_clariion.o hp_sw.o

all: $(BUILD)

prepare:
	@file *-$(BUILD).a >/dev/null 2>&1 || rm -f core *.o *.gz

klibc: prepare $(OBJS)
	ar rs libcheckers-klibc.a *.o

glibc: prepare $(OBJS)
	ar rs libcheckers-glibc.a *.o

install:

uninstall:

clean:
	rm -f core *.a *.o *.gz
