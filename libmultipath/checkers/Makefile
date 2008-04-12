# Makefile
#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@free.fr>
#
include ../../Makefile.inc

LIBS= \
	libcheckreadsector0.so \
	libchecktur.so \
	libcheckdirectio.so \
	libcheckemc_clariion.so \
	libcheckhp_sw.so \
	libcheckrdac.so

CFLAGS += -I..

all: $(LIBS)

libcheck%.so: libsg.o %.o
	$(CC) $(SHARED_FLAGS) -o $@ $^

install:
	$(INSTALL_PROGRAM) -o root -g root -m 755 $(LIBS) $(libdir)

uninstall:
	rm -f $(libdir)/$(LIBS)

clean:
	rm -f core *.a *.o *.gz *.so
