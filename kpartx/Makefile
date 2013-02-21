# Makefile
#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#
include ../Makefile.inc

CFLAGS += -I. -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64

LIBDM_API_COOKIE = $(shell grep -Ecs '^[a-z]*[[:space:]]+dm_task_set_cookie' /usr/include/libdevmapper.h)

ifneq ($(strip $(LIBDM_API_COOKIE)),0)
	CFLAGS += -DLIBDM_API_COOKIE
endif

LDFLAGS = -ldevmapper
OBJS = bsd.o dos.o kpartx.o solaris.o unixware.o dasd.o sun.o \
       gpt.o mac.o ps3.o crc32.o lopart.o xstrncpy.o devmapper.o
EXEC = kpartx

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(OBJS) -o $(EXEC) $(LDFLAGS)
	$(GZIP) $(EXEC).8 > $(EXEC).8.gz

install: $(EXEC) $(EXEC).8
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -m 755 $(EXEC) $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(libudevdir)
	$(INSTALL_PROGRAM) -m 755 kpartx_id $(DESTDIR)$(libudevdir)
	$(INSTALL_PROGRAM) -d $(DESTDIR)/etc/udev/rules.d
	$(INSTALL_PROGRAM) -m 644 kpartx.rules $(DESTDIR)/etc/udev/rules.d/
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(mandir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).8.gz $(DESTDIR)$(mandir)

uninstall:
	rm -f $(DESTDIR)$(bindir)/$(EXEC)
	rm -f $(DESTDIR)$(mandir)/$(EXEC).8.gz
	rm -f $(DESTDIR)$(libudevdir)/kpartx_id

clean:
	rm -f core *.o $(EXEC) *.gz
