# Makefile
#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#
include ../Makefile.inc

SONAME=0
DEVLIB = libmultipath.so
LIBS = $(DEVLIB).$(SONAME)
LIBDEPS = -lpthread -ldl -ldevmapper

OBJS = memory.o parser.o vector.o devmapper.o callout.o \
       hwtable.o blacklist.o util.o dmparser.o config.o \
       structs.o discovery.o propsel.o dict.o \
       pgpolicies.o debug.o regex.o defaults.o uevent.o \
       switchgroup.o uxsock.o print.o alias.o log_pthread.o \
       log.o configure.o structs_vec.o sysfs.o prio.o checkers.o \
       lock.o waiter.o

LIBDM_API_FLUSH = $(shell grep -Ecs '^[a-z]*[[:space:]]+dm_task_no_flush' /usr/include/libdevmapper.h)

ifneq ($(strip $(LIBDM_API_FLUSH)),0)
	CFLAGS += -DLIBDM_API_FLUSH -D_GNU_SOURCE
endif

all: $(LIBS)

$(LIBS): $(OBJS)
	$(CC) $(SHARED_FLAGS) $(LIBDEPS) -Wl,-soname=$@ $(CFLAGS) -o $@ $(OBJS)
	ln -sf $@ $(DEVLIB)

install:
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(syslibdir)
	$(INSTALL_PROGRAM) -m 755 $(LIBS) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(INSTALL_PROGRAM) -m 755 -d $(DESTDIR)$(libdir)

uninstall:
	rm -f $(DESTDIR)$(syslibdir)/$(LIBS)

clean:
	rm -f core *.a *.o *.gz *.so *.so.*
