#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#
include ../Makefile.inc

EXEC      := multipath
MANPAGES  := multipath.8 multipath.conf.5
GENERATED := $(MANPAGES) multipath.rules tmpfiles.conf

CPPFLAGS += -I$(multipathdir) -I$(mpathutildir) -I$(mpathcmddir)
CFLAGS += $(BIN_CFLAGS)
LDFLAGS += $(BIN_LDFLAGS)
LIBDEPS += -L$(multipathdir) -lmultipath -L$(mpathutildir) -lmpathutil \
	-L$(mpathcmddir) -lmpathcmd -lpthread -ldevmapper -ldl -ludev

OBJS := main.o

all: $(EXEC) $(GENERATED)

$(EXEC): $(OBJS) $(multipathdir)/libmultipath.so $(mpathcmddir)/libmpathcmd.so
	@echo building $@ because of $?
	$(Q)$(CC) $(CFLAGS) $(OBJS) -o $(EXEC) $(LDFLAGS) $(LIBDEPS)

install:
	$(Q)$(INSTALL_PROGRAM) -d $(DESTDIR)$(bindir)
	$(Q)$(INSTALL_PROGRAM) -m 755 $(EXEC) $(DESTDIR)$(bindir)/
	$(Q)$(INSTALL_PROGRAM) -d $(DESTDIR)$(udevrulesdir)
	$(Q)$(INSTALL_PROGRAM) -m 644 11-dm-mpath.rules $(DESTDIR)$(udevrulesdir)
	$(Q)$(INSTALL_PROGRAM) -m 644 multipath.rules $(DESTDIR)$(udevrulesdir)/56-multipath.rules
	$(Q)$(INSTALL_PROGRAM) -d $(DESTDIR)$(modulesloaddir)
	$(Q)$(INSTALL_PROGRAM) -m 644 modules-load.conf $(DESTDIR)$(modulesloaddir)/multipath.conf
	$(Q)$(INSTALL_PROGRAM) -d $(DESTDIR)$(tmpfilesdir)
	$(Q)$(INSTALL_PROGRAM) -m 644 tmpfiles.conf $(DESTDIR)$(tmpfilesdir)/multipath.conf
	$(Q)$(INSTALL_PROGRAM) -d $(DESTDIR)$(mandir)/man8
	$(Q)$(INSTALL_PROGRAM) -m 644 $(EXEC).8 $(DESTDIR)$(mandir)/man8
	$(Q)$(INSTALL_PROGRAM) -d $(DESTDIR)$(mandir)/man5
	$(Q)$(INSTALL_PROGRAM) -m 644 $(EXEC).conf.5 $(DESTDIR)$(mandir)/man5
ifneq ($(SCSI_DH_MODULES_PRELOAD),)
	$(Q)$(INSTALL_PROGRAM) -m 644 scsi_dh.conf $(DESTDIR)$(modulesloaddir)/scsi_dh.conf
	$(Q)for _x in $(SCSI_DH_MODULES_PRELOAD); do echo "$$_x"; done \
	    >>$(DESTDIR)$(modulesloaddir)/scsi_dh.conf
endif

uninstall:
	$(Q)$(RM) $(DESTDIR)$(bindir)/$(EXEC)
	$(Q)$(RM) $(DESTDIR)$(udevrulesdir)/11-dm-mpath.rules
	$(Q)$(RM) $(DESTDIR)$(modulesloaddir)/multipath.conf
	$(Q)$(RM) $(DESTDIR)$(modulesloaddir)/scsi_dh.conf
	$(Q)$(RM) $(DESTDIR)$(libudevdir)/rules.d/56-multipath.rules
	$(Q)$(RM) $(DESTDIR)$(mandir)/man8/$(EXEC).8
	$(Q)$(RM) $(DESTDIR)$(mandir)/man5/$(EXEC).conf.5
	$(Q)$(RM) $(DESTDIR)$(tmpfilesdir)/multipath.conf

clean: dep_clean
	$(Q)$(RM) core *.o $(EXEC) $(GENERATED)

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(Q)$(RM) $(OBJS:.o=.d)
