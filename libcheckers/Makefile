# Makefile
#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@free.fr>
#
BUILD = glibc

include ../Makefile.inc

OBJS = readsector0.o tur.o selector.o emc_clariion.o

all: $(BUILD)

prepare:
	rm -f core *.o *.gz

klibc: prepare $(OBJS)
	ar rs libcheckers-klibc.a *.o

glibc: prepare $(OBJS)
	ar rs libcheckers-glibc.a *.o

install:

uninstall:

clean:
	rm -f core *.a *.o *.gz
