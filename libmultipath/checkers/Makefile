#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#
include ../../Makefile.inc

CFLAGS += -I..

LIBS= \
	libcheckcciss_tur.so \
	libcheckreadsector0.so \
	libchecktur.so \
	libcheckdirectio.so \
	libcheckemc_clariion.so \
	libcheckhp_sw.so \
	libcheckrdac.so \
	libcheckrbd.so

all: $(LIBS)

libcheckrbd.so: rbd.o
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -o $@ $^ -lrados -ludev

libcheckdirectio.so: libsg.o directio.o
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -o $@ $^ -laio

libcheck%.so: libsg.o %.o
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -o $@ $^

install:
	$(INSTALL_PROGRAM) -m 755 $(LIBS) $(DESTDIR)$(libdir)

uninstall:
	for file in $(LIBS); do $(RM) $(DESTDIR)$(libdir)/$$file; done

clean:
	$(RM) core *.a *.o *.gz *.so
