#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#
include ../Makefile.inc

DEVLIB := libmultipath.so
CPPFLAGS += -I$(mpathutildir) -I$(mpathcmddir) -I$(nvmedir) -D_GNU_SOURCE $(SYSTEMD_CPPFLAGS)
CFLAGS += $(LIB_CFLAGS)
LIBDEPS += -lpthread -ldl -ldevmapper -ludev -L$(mpathutildir) -lmpathutil -L$(mpathcmddir) -lmpathcmd \
	-lurcu -laio $(SYSTEMD_LIBDEPS)

# object files referencing MULTIPATH_DIR or CONFIG_DIR
# they need to be recompiled for unit tests
OBJS-U := prio.o checkers.o foreign.o config.o
OBJS-T := $(patsubst %.o,%-test.o,$(OBJS-U))

# other object files
OBJS-O := devmapper.o hwtable.o blacklist.o dmparser.o \
	structs.o discovery.o propsel.o dict.o \
	pgpolicies.o defaults.o uevent.o \
	switchgroup.o print.o alias.o \
	configure.o structs_vec.o sysfs.o \
	lock.o file.o wwids.o prioritizers/alua_rtpg.o prkey.o \
	io_err_stat.o dm-generic.o generic.o nvme-lib.o \
	libsg.o valid.o

OBJS := $(OBJS-O) $(OBJS-U)

all:	$(DEVLIB)

include $(TOPDIR)/rules.mk

nvme-lib.o: nvme-lib.c nvme-ioctl.c nvme-ioctl.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-unused-function -c -o $@ $<

# there are lots of "unused parameters" in dict.c
# because not all handler / snprint methods need all parameters
dict.o:	dict.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-unused-parameter -c -o $@ $<

make_static = $(shell sed '/^static/!s/^\([a-z]\{1,\} \)/static \1/' <$1 >$2)

nvme-ioctl.c: nvme/nvme-ioctl.c
	$(call make_static,$<,$@)

nvme-ioctl.h: nvme/nvme-ioctl.h
	$(call make_static,$<,$@)

../tests/$(LIBS): $(OBJS-O) $(OBJS-T) $(VERSION_SCRIPT)
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -Wl,-soname=`basename $@` \
		-o $@ $(OBJS-O) $(OBJS-T) $(LIBDEPS)
	$(LN) $@ ${@:.so.0=.so}

# This rule is invoked from tests/Makefile, overriding configdir and plugindir
%-test.o: %.c
	@echo building $@ because of $?
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

test-lib:	../tests/$(LIBS)

install: all
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(syslibdir)
	$(INSTALL_PROGRAM) -m 755 $(LIBS) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(INSTALL_PROGRAM) -m 755 -d $(DESTDIR)$(plugindir)
	$(LN) $(LIBS) $(DESTDIR)$(syslibdir)/$(DEVLIB)

uninstall:
	$(RM) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(RM) $(DESTDIR)$(syslibdir)/$(DEVLIB)

clean: dep_clean
	$(RM) core *.a *.o *.so *.so.* *.abi nvme-ioctl.c nvme-ioctl.h autoconfig.h $(NV_VERSION_SCRIPT)

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(RM) $(OBJS:.o=.d)
