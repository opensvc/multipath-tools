#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#
include ../Makefile.inc

CPPFLAGS += -I$(multipathdir) -I$(mpathutildir) -I$(mpathcmddir)
CFLAGS += $(BIN_CFLAGS)
LDFLAGS += $(BIN_LDFLAGS)
LIBDEPS += -L$(multipathdir) -lmultipath -L$(mpathutildir) -lmpathutil \
	-L$(mpathcmddir) -lmpathcmd -lpthread -ldevmapper -ldl -ludev

EXEC = multipath

OBJS = main.o

all: $(EXEC) multipath.rules tmpfiles.conf

$(EXEC): $(OBJS) $(multipathdir)/libmultipath.so $(mpathcmddir)/libmpathcmd.so
	$(CC) $(CFLAGS) $(OBJS) -o $(EXEC) $(LDFLAGS) $(LIBDEPS)

install:
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -m 755 $(EXEC) $(DESTDIR)$(bindir)/
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(udevrulesdir)
	$(INSTALL_PROGRAM) -m 644 11-dm-mpath.rules $(DESTDIR)$(udevrulesdir)
	$(INSTALL_PROGRAM) -m 644 multipath.rules $(DESTDIR)$(udevrulesdir)/56-multipath.rules
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(modulesloaddir)
	$(INSTALL_PROGRAM) -m 644 modules-load.conf $(DESTDIR)$(modulesloaddir)/multipath.conf
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(tmpfilesdir)
	$(INSTALL_PROGRAM) -m 644 tmpfiles.conf $(DESTDIR)$(tmpfilesdir)/multipath.conf
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(man8dir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).8 $(DESTDIR)$(man8dir)
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(man5dir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).conf.5 $(DESTDIR)$(man5dir)
ifneq ($(SCSI_DH_MODULES_PRELOAD),)
	$(INSTALL_PROGRAM) -m 644 scsi_dh.conf $(DESTDIR)$(modulesloaddir)/scsi_dh.conf
	for _x in $(SCSI_DH_MODULES_PRELOAD); do echo "$$_x"; done \
	    >>$(DESTDIR)$(modulesloaddir)/scsi_dh.conf
endif

uninstall:
	$(RM) $(DESTDIR)$(bindir)/$(EXEC)
	$(RM) $(DESTDIR)$(udevrulesdir)/11-dm-mpath.rules
	$(RM) $(DESTDIR)$(modulesloaddir)/multipath.conf
	$(RM) $(DESTDIR)$(modulesloaddir)/scsi_dh.conf
	$(RM) $(DESTDIR)$(libudevdir)/rules.d/56-multipath.rules
	$(RM) $(DESTDIR)$(man8dir)/$(EXEC).8
	$(RM) $(DESTDIR)$(man5dir)/$(EXEC).conf.5

clean: dep_clean
	$(RM) core *.o $(EXEC) multipath.rules tmpfiles.conf

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(RM) $(OBJS:.o=.d)

%:	%.in
	sed 's,@RUNTIME_DIR@,$(runtimedir),' $< >$@
