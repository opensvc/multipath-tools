#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#
include ../Makefile.inc

CPPFLAGS += -I. -I$(multipathdir) -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
CFLAGS += $(BIN_CFLAGS)
LDFLAGS += $(BIN_LDFLAGS)

LIBDEPS += -ldevmapper

ifneq ($(call check_func,dm_task_set_cookie,$(devmapper_incdir)/libdevmapper.h),0)
	CPPFLAGS += -DLIBDM_API_COOKIE
endif

OBJS = bsd.o dos.o kpartx.o solaris.o unixware.o dasd.o sun.o \
	gpt.o mac.o ps3.o crc32.o lopart.o xstrncpy.o devmapper.o

EXEC = kpartx

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(EXEC) $(LDFLAGS) $(LIBDEPS)

install: $(EXEC) $(EXEC).8
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -m 755 $(EXEC) $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(libudevdir)
	$(INSTALL_PROGRAM) -m 755 kpartx_id $(DESTDIR)$(libudevdir)
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(libudevdir)/rules.d
	$(INSTALL_PROGRAM) -m 644 dm-parts.rules $(DESTDIR)$(libudevdir)/rules.d/11-dm-parts.rules
	$(INSTALL_PROGRAM) -m 644 kpartx.rules $(DESTDIR)$(libudevdir)/rules.d/66-kpartx.rules
	$(INSTALL_PROGRAM) -m 644 del-part-nodes.rules $(DESTDIR)$(libudevdir)/rules.d/68-del-part-nodes.rules
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(mandir)/man8
	$(INSTALL_PROGRAM) -m 644 $(EXEC).8 $(DESTDIR)$(mandir)/man8

uninstall:
	$(RM) $(DESTDIR)$(bindir)/$(EXEC)
	$(RM) $(DESTDIR)$(mandir)/man8/$(EXEC).8
	$(RM) $(DESTDIR)$(libudevdir)/kpartx_id
	$(RM) $(DESTDIR)$(libudevdir)/rules.d/11-dm-parts.rules
	$(RM) $(DESTDIR)$(libudevdir)/rules.d/66-kpartx.rules
	$(RM) $(DESTDIR)$(libudevdir)/rules.d/67-kpartx-compat.rules
	$(RM) $(DESTDIR)$(libudevdir)/rules.d/68-del-part-nodes.rules

clean: dep_clean
	$(RM) core *.o $(EXEC)

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(RM) $(OBJS:.o=.d)
