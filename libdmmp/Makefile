# Makefile
#
# Copyright (C) 2015 - 2016 Red Hat, Inc.
# Gris Ge <fge@redhat.com>
#
include ../Makefile.inc

LIBDMMP_VERSION=0.2.0
SONAME=$(LIBDMMP_VERSION)
DEVLIB = libdmmp.so
LIBS = $(DEVLIB).$(SONAME)
PKGFILE = libdmmp.pc
EXTRA_MAN_FILES = libdmmp.h.3
HEADERS = libdmmp/libdmmp.h

OBJS = libdmmp.o libdmmp_mp.o libdmmp_pg.o libdmmp_path.o libdmmp_misc.o

CFLAGS += $(LIB_CFLAGS) -fvisibility=hidden -I$(libdmmpdir) -I$(mpathcmddir) \
	  $(shell $(PKGCONFIG) --cflags json-c)

LIBDEPS += $(shell $(PKGCONFIG) --libs json-c) -L$(mpathcmddir) -lmpathcmd -lpthread

all: $(LIBS) doc
.PHONY:	doc doc.gz clean install uninstall check speed_test dep_clean

$(LIBS): $(OBJS)
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -Wl,-soname=$@ -o $@ $(OBJS) $(LIBDEPS)
	$(LN) $@ $(DEVLIB)

install:	doc.gz
	mkdir -p $(DESTDIR)$(usrlibdir)
	$(INSTALL_PROGRAM) -m 755 $(LIBS) $(DESTDIR)$(usrlibdir)/$(LIBS)
	$(INSTALL_PROGRAM) -m 644 -D \
		$(HEADERS) $(DESTDIR)$(includedir)/$(HEADERS)
	$(LN) $(LIBS) $(DESTDIR)$(usrlibdir)/$(DEVLIB)
	$(INSTALL_PROGRAM) -m 644 -D \
		$(PKGFILE).in $(DESTDIR)$(pkgconfdir)/$(PKGFILE)
	perl -i -pe 's|__VERSION__|$(LIBDMMP_VERSION)|g' \
		$(DESTDIR)$(pkgconfdir)/$(PKGFILE)
	perl -i -pe 's|__LIBDIR__|$(usrlibdir)|g' \
		$(DESTDIR)$(pkgconfdir)/$(PKGFILE)
	perl -i -pe 's|__INCLUDEDIR__|$(includedir)|g' \
		$(DESTDIR)$(pkgconfdir)/$(PKGFILE)
	$(INSTALL_PROGRAM) -m 644 -t $(DESTDIR)$(man3dir) docs/man/*.3.gz

uninstall:
	$(RM) $(DESTDIR)$(usrlibdir)/$(LIBS)
	$(RM) $(DESTDIR)$(includedir)/$(HEADERS)
	$(RM) $(DESTDIR)$(usrlibdir)/$(DEVLIB)
	@for file in $(DESTDIR)$(man3dir)/dmmp_*; do \
		$(RM) $$file; \
	done
	$(RM) $(DESTDIR)$(man3dir)/libdmmp.h*
	$(RM) $(DESTDIR)$(pkgconfdir)/$(PKGFILE)

clean: dep_clean
	$(RM) core *.a *.o *.gz *.so *.so.*
	$(RM) docs/man/*.gz
	$(MAKE) -C test clean

include $(wildcard $(OBJS:.o=.d))

check: all
	$(MAKE) -C test check

speed_test: all
	$(MAKE) -C test speed_test

doc.gz:	doc $(patsubst %,%.gz,$(wildcard docs/man/*.3))

doc: docs/man/dmmp_strerror.3

docs/man/%.3.gz:	docs/man/%.3
	gzip -c $< >$@

docs/man/dmmp_strerror.3:	$(HEADERS)
	TEMPFILE=$(shell mktemp); \
	cat $^ | perl docs/doc-preclean.pl >$$TEMPFILE; \
	LC_ALL=C \
	KBUILD_BUILD_TIMESTAMP=`git log -n1 --pretty=%cd --date=iso -- $^` \
	perl docs/kernel-doc -man $$TEMPFILE | \
	    perl docs/split-man.pl docs/man; \
	rm -f $$TEMPFILE

dep_clean:
	$(RM) $(OBJS:.o=.d)
