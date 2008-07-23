# Makefile
#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@free.fr>
#
include ../../Makefile.inc

LIBS= \
	libcheckcciss_tur.so \
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
	$(INSTALL_PROGRAM) -m 755 $(LIBS) $(DESTDIR)$(libdir)

uninstall:
	rm -f $(DESTDIR)$(libdir)/$(LIBS)

clean:
	rm -f core *.a *.o *.gz *.so
