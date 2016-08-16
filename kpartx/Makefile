#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#
include ../Makefile.inc

CFLAGS += -I. -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64

LIBDEPS += -ldevmapper

LIBDM_API_COOKIE = $(shell grep -Ecs '^[a-z]*[[:space:]]+dm_task_set_cookie' /usr/include/libdevmapper.h)

ifneq ($(strip $(LIBDM_API_COOKIE)),0)
	CFLAGS += -DLIBDM_API_COOKIE
endif

OBJS = bsd.o dos.o kpartx.o solaris.o unixware.o dasd.o sun.o \
	gpt.o mac.o ps3.o crc32.o lopart.o xstrncpy.o devmapper.o

EXEC = kpartx

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(EXEC) $(LDFLAGS) $(LIBDEPS)
	$(GZIP) $(EXEC).8 > $(EXEC).8.gz

install: $(EXEC) $(EXEC).8
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -m 755 $(EXEC) $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(libudevdir)
	$(INSTALL_PROGRAM) -m 755 kpartx_id $(DESTDIR)$(libudevdir)
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(libudevdir)/rules.d
	$(INSTALL_PROGRAM) -m 644 kpartx.rules $(DESTDIR)$(libudevdir)/rules.d/66-kpartx.rules
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(mandir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).8.gz $(DESTDIR)$(mandir)

uninstall:
	$(RM) $(DESTDIR)$(bindir)/$(EXEC)
	$(RM) $(DESTDIR)$(mandir)/$(EXEC).8.gz
	$(RM) $(DESTDIR)$(libudevdir)/kpartx_id
	$(RM) $(DESTDIR)$(libudevdir)/rules.d/66-kpartx.rules
	$(RM) $(DESTDIR)$(libudevdir)/rules.d/67-kpartx-compat.rules

clean:
	$(RM) core *.o $(EXEC) *.gz
