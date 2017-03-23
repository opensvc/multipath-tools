#
# Copyright (C) 2007 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#
include ../../Makefile.inc

CFLAGS += -I..

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
	libpriosysfs.so

all: $(LIBS)

libprioalua.so: alua.o alua_rtpg.o
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -o $@ $^

libprio%.so: %.o
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -o $@ $^

install: $(LIBS)
	$(INSTALL_PROGRAM) -m 755 libprio*.so $(DESTDIR)$(libdir)

uninstall:
	for file in $(LIBS); do $(RM) $(DESTDIR)$(libdir)/$$file; done

clean:
	$(RM) core *.a *.o *.gz *.so
