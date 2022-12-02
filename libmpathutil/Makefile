#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#
include ../Makefile.inc

DEVLIB := libmpathutil.so
CPPFLAGS += -I. -I$(multipathdir) -I$(mpathcmddir) $(SYSTEMD_CPPFLAGS)
CFLAGS += $(LIB_CFLAGS) -D_GNU_SOURCE
LIBDEPS += -lpthread -ldl -ludev -L$(mpathcmddir) -lmpathcmd $(SYSTEMD_LIBDEPS) -lrt

# object files referencing MULTIPATH_DIR or CONFIG_DIR
# they need to be recompiled for unit tests

# other object files
OBJS := parser.o vector.o util.o debug.o time-util.o \
	uxsock.o log_pthread.o log.o strbuf.o globals.o msort.o

all:	$(DEVLIB)

include $(TOPDIR)/rules.mk

install: all
	$(Q)$(INSTALL_PROGRAM) -d $(DESTDIR)$(syslibdir)
	$(Q)$(INSTALL_PROGRAM) -m 755 $(LIBS) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(Q)$(LN) $(LIBS) $(DESTDIR)$(syslibdir)/$(DEVLIB)

uninstall:
	$(Q)$(RM) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(Q)$(RM) $(DESTDIR)$(syslibdir)/$(DEVLIB)

clean: dep_clean
	$(Q)$(RM) core *.a *.o *.so *.so.* *.abi nvme-ioctl.c nvme-ioctl.h $(NV_VERSION_SCRIPT)

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(Q)$(RM) $(OBJS:.o=.d)
