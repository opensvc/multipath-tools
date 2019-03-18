#
# Copyright (C) 2007 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#
include ../../Makefile.inc

CFLAGS += $(LIB_CFLAGS) -I..

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

ifneq ($(call check_file,/usr/include/linux/nvme_ioctl.h),0)
	LIBS += libprioana.so
	CFLAGS += -I../nvme
endif

all: $(LIBS)

libpriopath_latency.so: path_latency.o  ../checkers/libsg.o
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -o $@ $^ -lm

libprio%.so: %.o
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -o $@ $^

install: $(LIBS)
	$(INSTALL_PROGRAM) -m 755 libprio*.so $(DESTDIR)$(libdir)

uninstall:
	for file in $(LIBS); do $(RM) $(DESTDIR)$(libdir)/$$file; done

clean: dep_clean
	$(RM) core *.a *.o *.gz *.so

OBJS = $(LIBS:libprio%.so=%.o) alua_rtpg.o
.SECONDARY: $(OBJS)

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(RM) $(OBJS:.o=.d)
