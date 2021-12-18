#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#
include ../Makefile.inc

SONAME = 0
DEVLIB = libmultipath.so
LIBS = $(DEVLIB).$(SONAME)
VERSION_SCRIPT := libmultipath.version

CFLAGS += $(LIB_CFLAGS) -I$(mpathcmddir) -I$(mpathpersistdir) -I$(nvmedir)

LIBDEPS += -lpthread -ldl -ldevmapper -ludev -L$(mpathcmddir) -lmpathcmd -lurcu -laio

ifdef SYSTEMD
	CFLAGS += -DUSE_SYSTEMD=$(SYSTEMD)
	ifeq ($(shell test $(SYSTEMD) -gt 209 && echo 1), 1)
		LIBDEPS += -lsystemd
	else
		LIBDEPS += -lsystemd-daemon
	endif
endif

ifneq ($(call check_func,dm_task_no_flush,/usr/include/libdevmapper.h),0)
	CFLAGS += -DLIBDM_API_FLUSH -D_GNU_SOURCE
endif

ifneq ($(call check_func,dm_task_get_errno,/usr/include/libdevmapper.h),0)
	CFLAGS += -DLIBDM_API_GET_ERRNO
endif

ifneq ($(call check_func,dm_task_set_cookie,/usr/include/libdevmapper.h),0)
	CFLAGS += -DLIBDM_API_COOKIE
endif

ifneq ($(call check_func,udev_monitor_set_receive_buffer_size,/usr/include/libudev.h),0)
	CFLAGS += -DLIBUDEV_API_RECVBUF
endif

ifneq ($(call check_func,dm_task_deferred_remove,/usr/include/libdevmapper.h),0)
	CFLAGS += -DLIBDM_API_DEFERRED
endif

ifneq ($(call check_func,dm_hold_control_dev,/usr/include/libdevmapper.h),0)
	CFLAGS += -DLIBDM_API_HOLD_CONTROL
endif

OBJS = parser.o vector.o devmapper.o callout.o \
	hwtable.o blacklist.o util.o dmparser.o config.o \
	structs.o discovery.o propsel.o dict.o \
	pgpolicies.o debug.o defaults.o uevent.o time-util.o \
	switchgroup.o uxsock.o print.o alias.o log_pthread.o \
	log.o configure.o structs_vec.o sysfs.o prio.o checkers.o \
	lock.o file.o wwids.o prioritizers/alua_rtpg.o prkey.o \
	io_err_stat.o dm-generic.o generic.o foreign.o nvme-lib.o \
	libsg.o valid.o strbuf.o

all:	$(DEVLIB)

nvme-lib.o: nvme-lib.c nvme-ioctl.c nvme-ioctl.h
	$(CC) $(CFLAGS) -Wno-unused-function -c -o $@ $<

# there are lots of "unused parameters" in dict.c
# because not all handler / snprint methods need all parameters
dict.o:	dict.c
	$(CC) $(CFLAGS) -Wno-unused-parameter -c -o $@ $<

make_static = $(shell sed '/^static/!s/^\([a-z]\{1,\} \)/static \1/' <$1 >$2)

nvme-ioctl.c: nvme/nvme-ioctl.c
	$(call make_static,$<,$@)

nvme-ioctl.h: nvme/nvme-ioctl.h
	$(call make_static,$<,$@)


$(LIBS): $(OBJS) $(VERSION_SCRIPT)
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -Wl,-soname=$@ \
		-Wl,--version-script=$(VERSION_SCRIPT) -o $@ $(OBJS) $(LIBDEPS)

$(DEVLIB): $(LIBS)
	$(LN) $(LIBS) $@

$(NV_VERSION_SCRIPT):	$(VERSION_SCRIPT)
	@printf 'NOVERSION {\nglobal:\n' >$@
	@grep -P '^[ \t]+[a-zA-Z_][a-zA-Z0-9_]*;' $< >>$@
	@printf 'local:\n\t*;\n};\n' >>$@

$(LIBS:%.so.$(SONAME)=%-nv.so):	$(OBJS) $(NV_VERSION_SCRIPT)
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -Wl,-soname=$@ \
		-Wl,--version-script=$(NV_VERSION_SCRIPT) -o $@ $(OBJS) $(LIBDEPS)

abi:    $(LIBS:%.so.$(SONAME)=%-nv.abi)

../tests/$(LIBS): $(OBJS) $(VERSION_SCRIPT)
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -Wl,-soname=`basename $@` \
		-o $@ $(OBJS) $(LIBDEPS)
	$(LN) $@ ${@:.so.0=.so}

test-lib:	../tests/$(LIBS)

install: all
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(syslibdir)
	$(INSTALL_PROGRAM) -m 755 $(LIBS) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(INSTALL_PROGRAM) -m 755 -d $(DESTDIR)$(libdir)
	$(LN) $(LIBS) $(DESTDIR)$(syslibdir)/$(DEVLIB)

uninstall:
	$(RM) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(RM) $(DESTDIR)$(syslibdir)/$(DEVLIB)

clean: dep_clean
	$(RM) core *.a *.o *.so *.so.* *.gz *.abi nvme-ioctl.c nvme-ioctl.h $(NV_VERSION_SCRIPT)

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(RM) $(OBJS:.o=.d)
