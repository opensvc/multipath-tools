# Makefile
#
# Copyright (C) 2007 Christophe Varoqui, <christophe.varoqui@free.fr>
#
include ../Makefile.inc

LIBS = \
	libprio.so \
	libpriorandom.so \
	libprioconst.so \
	libpriohp_sw.so \
	libprioemc.so \
	libpriordac.so \
	libprioalua.so \
	libprionetapp.so \
	libpriohds.so

CFLAGS += -I$(multipathdir)
SHARED_FLAGS += -shared -fPIC

all: $(LIBS)

libprioalua.so: alua.o alua_rtpg.o
	$(CC) $(SHARED_FLAGS) -o $@ $^

%.so: %.o
	$(CC) $(SHARED_FLAGS) -o $@ $^

libprio%.so: %.o
	$(CC) $(SHARED_FLAGS) -o $@ $^

install: $(LIBS)
	install -m 755 libprio*.so $(libdir)

uninstall:
	rm -f $(libdir)/libprio*.so

clean:
	rm -f core *.a *.o *.gz *.so
