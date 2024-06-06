#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#
include ../Makefile.inc

EXEC := kpartx

CPPFLAGS += -I. -I$(multipathdir) -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
CFLAGS += $(BIN_CFLAGS)
LDFLAGS += $(BIN_LDFLAGS)
LIBDEPS += -ldevmapper

OBJS := bsd.o dos.o kpartx.o solaris.o unixware.o dasd.o sun.o \
	gpt.o mac.o ps3.o crc32.o lopart.o xstrncpy.o devmapper.o

all: $(EXEC) kpartx.rules

$(EXEC): $(OBJS)
	@echo building $@ because of $?
	$(Q)$(CC) $(CFLAGS) $(OBJS) -o $(EXEC) $(LDFLAGS) $(LIBDEPS)

install: $(EXEC) $(EXEC).8 kpartx.rules
	$(Q)$(INSTALL_PROGRAM) -d $(DESTDIR)$(bindir)
	$(Q)$(INSTALL_PROGRAM) -m 755 $(EXEC) $(DESTDIR)$(bindir)
	$(Q)$(INSTALL_PROGRAM) -d $(DESTDIR)$(libudevdir)
	$(Q)$(INSTALL_PROGRAM) -m 755 kpartx_id $(DESTDIR)$(libudevdir)
	$(Q)$(INSTALL_PROGRAM) -d $(DESTDIR)$(libudevdir)/rules.d
	$(Q)$(INSTALL_PROGRAM) -m 644 dm-parts.rules $(DESTDIR)$(libudevdir)/rules.d/11-dm-parts.rules
	$(Q)$(INSTALL_PROGRAM) -m 644 kpartx.rules $(DESTDIR)$(libudevdir)/rules.d/66-kpartx.rules
	$(Q)$(INSTALL_PROGRAM) -m 644 del-part-nodes.rules $(DESTDIR)$(libudevdir)/rules.d/68-del-part-nodes.rules
	$(Q)$(INSTALL_PROGRAM) -d $(DESTDIR)$(mandir)/man8
	$(Q)$(INSTALL_PROGRAM) -m 644 $(EXEC).8 $(DESTDIR)$(mandir)/man8

uninstall:
	$(Q)$(RM) $(DESTDIR)$(bindir)/$(EXEC)
	$(Q)$(RM) $(DESTDIR)$(mandir)/man8/$(EXEC).8
	$(Q)$(RM) $(DESTDIR)$(libudevdir)/kpartx_id
	$(Q)$(RM) $(DESTDIR)$(libudevdir)/rules.d/11-dm-parts.rules
	$(Q)$(RM) $(DESTDIR)$(libudevdir)/rules.d/66-kpartx.rules
	$(Q)$(RM) $(DESTDIR)$(libudevdir)/rules.d/67-kpartx-compat.rules
	$(Q)$(RM) $(DESTDIR)$(libudevdir)/rules.d/68-del-part-nodes.rules

clean: dep_clean
	$(Q)$(RM) core *.o $(EXEC) kpartx.rules

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(Q)$(RM) $(OBJS:.o=.d)
