# Makefile
#
# Copyright (C) 2007 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#
include ../../Makefile.inc

LIBS = \
	libpriorandom.so \
	libprioconst.so \
	libpriohp_sw.so \
	libprioemc.so \
	libpriordac.so \
	libprioalua.so \
	libprioontap.so \
	libpriodatacore.so \
	libpriohds.so \
	libprioweightedpath.so \
	libprioiet.so

CFLAGS += -I..

all: $(LIBS)

libprioalua.so: alua.o alua_rtpg.o
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -o $@ $^

libprio%.so: %.o
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -o $@ $^

install: $(LIBS)
	$(INSTALL_PROGRAM) -m 755 libprio*.so $(DESTDIR)$(libdir)

uninstall:
	for file in $(LIBS); do rm -f $(DESTDIR)$(libdir)/$$file; done

clean:
	rm -f core *.a *.o *.gz *.so
