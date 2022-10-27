#
# Copyright (C) 2007 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#
TOPDIR=../..

include ../../Makefile.inc

CPPFLAGS += -I$(multipathdir) -I$(mpathutildir)
CFLAGS += $(LIB_CFLAGS)
LDFLAGS += -L$(multipathdir) -L$(mpathutildir)
LIBDEPS = -lmultipath -lmpathutil -lm -lpthread -lrt

# If you add or remove a prioritizer also update multipath/multipath.conf.5
LIBS = \
	libprioalua.so \
	libprioconst.so \
	libpriodatacore.so \
	libprioemc.so \
	libpriohds.so \
	libpriohp_sw.so \
	libprioiet.so \
	libprioontap.so \
	libpriorandom.so \
	libpriordac.so \
	libprioweightedpath.so \
	libpriopath_latency.so \
	libpriosysfs.so

ifneq ($(call check_file,$(kernel_incdir)/linux/nvme_ioctl.h),0)
	LIBS += libprioana.so
	CPPFLAGS += -I../nvme
endif

all: $(LIBS)

libprio%.so: %.o
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -o $@ $^ $(LIBDEPS)

install: $(LIBS)
	$(INSTALL_PROGRAM) -m 755 libprio*.so $(DESTDIR)$(plugindir)

uninstall:
	for file in $(LIBS); do $(RM) $(DESTDIR)$(plugindir)/$$file; done

clean: dep_clean
	$(RM) core *.a *.o *.gz *.so

OBJS = $(LIBS:libprio%.so=%.o) alua_rtpg.o
.SECONDARY: $(OBJS)

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(RM) $(OBJS:.o=.d)
