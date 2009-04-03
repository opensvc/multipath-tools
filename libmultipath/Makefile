# Makefile
#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@free.fr>
#
include ../Makefile.inc

LIBS = libmultipath.so

OBJS = memory.o parser.o vector.o devmapper.o callout.o \
       hwtable.o blacklist.o util.o dmparser.o config.o \
       structs.o discovery.o propsel.o dict.o \
       pgpolicies.o debug.o regex.o defaults.o uevent.o \
       switchgroup.o uxsock.o print.o alias.o log_pthread.o \
       log.o configure.o structs_vec.o sysfs.o prio.o checkers.o \
       lock.o waiter.o

LIBDM_API_FLUSH = $(shell if test -d /lib64 ; then objdump -T /lib64/libdevmapper.so* ; else objdump -T /lib/libdevmapper.so.* ; fi | grep -c dm_task_no_flush)

ifneq ($(strip $(LIBDM_API_FLUSH)),0)
	CFLAGS += -DLIBDM_API_FLUSH
endif

all: $(LIBS)

$(LIBS): $(OBJS)
	$(CC) $(SHARED_FLAGS) -Wl,-soname,$@ $(CFLAGS) -o $@ $(OBJS)

install:
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(prefix)/lib
	$(INSTALL_PROGRAM) -m 755 $(LIBS) $(DESTDIR)$(prefix)/lib/$(LIBS)
	$(INSTALL_PROGRAM) -m 755 -d $(DESTDIR)$(libdir)

uninstall:
	rm -f $(DESTDIR)$(prefix)/lib/$(LIBS)

clean:
	rm -f core *.a *.o *.gz *.so
