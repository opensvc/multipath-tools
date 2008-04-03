# Makefile
#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@free.fr>
#
include ../Makefile.inc

LIBS = libcheckers.so
OBJS = libsg.o checkers.o readsector0.o tur.o directio.o emc_clariion.o hp_sw.o rdac.o

SHARED_FLAGS = -shared

all: $(LIBS)

$(LIBS): $(OBJS)
	$(CC) $(SHARED_FLAGS) -o $@ $^

install:
	$(INSTALL_PROGRAM) -o root -g root -m 755 $(LIBS) $(libdir)

uninstall:
	rm -f $(libdir)/$(LIBS)

clean:
	rm -f core *.a *.o *.gz *.so
