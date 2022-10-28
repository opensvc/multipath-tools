# Makefile
#
# Copyright (C) 2015 - 2016 Red Hat, Inc.
# Gris Ge <fge@redhat.com>
#
include ../Makefile.inc

LIBDMMP_VERSION := 0.2.0
SONAME := $(LIBDMMP_VERSION)
DEVLIB := libdmmp.so
PKGFILE := libdmmp.pc
EXTRA_MAN_FILES := libdmmp.h.3
HEADERS := libdmmp/libdmmp.h

OBJS := libdmmp.o libdmmp_mp.o libdmmp_pg.o libdmmp_path.o libdmmp_misc.o

CPPFLAGS += -I$(libdmmpdir) -I$(mpathcmddir) $(shell $(PKGCONFIG) --cflags json-c)
CFLAGS += $(LIB_CFLAGS) -fvisibility=hidden

LIBDEPS += $(shell $(PKGCONFIG) --libs json-c) -L$(mpathcmddir) -lmpathcmd -lpthread

all: $(LIBS) doc
.PHONY:	doc clean install uninstall check speed_test dep_clean

$(LIBS): $(OBJS)
	$(Q)$(CC) $(LDFLAGS) $(SHARED_FLAGS) -Wl,-soname=$@ -o $@ $(OBJS) $(LIBDEPS)

$(DEVLIB): $(LIBS)
	$(Q)$(LN) $(LIBS) $@

abi:    $(DEVLIB:%.so=%.abi)

install:
	@mkdir -p $(DESTDIR)$(usrlibdir)
	$(Q)$(INSTALL_PROGRAM) -m 755 $(LIBS) $(DESTDIR)$(usrlibdir)/$(LIBS)
	$(Q)$(INSTALL_PROGRAM) -m 644 -D \
		$(HEADERS) $(DESTDIR)$(includedir)/$(HEADERS)
	$(Q)$(LN) $(LIBS) $(DESTDIR)$(usrlibdir)/$(DEVLIB)
	$(Q)$(INSTALL_PROGRAM) -m 644 -D \
		$(PKGFILE).in $(DESTDIR)$(pkgconfdir)/$(PKGFILE)
	$(Q)perl -i -pe 's|__VERSION__|$(LIBDMMP_VERSION)|g' \
		$(DESTDIR)$(pkgconfdir)/$(PKGFILE)
	$(Q)perl -i -pe 's|__LIBDIR__|$(usrlibdir)|g' \
		$(DESTDIR)$(pkgconfdir)/$(PKGFILE)
	$(Q)perl -i -pe 's|__INCLUDEDIR__|$(includedir)|g' \
		$(DESTDIR)$(pkgconfdir)/$(PKGFILE)
	$(Q)$(INSTALL_PROGRAM) -d 755 $(DESTDIR)$(mandir)/man3
	$(Q)$(INSTALL_PROGRAM) -m 644 -t $(DESTDIR)$(mandir)/man3 docs/man/*.3

uninstall:
	$(Q)$(RM) $(DESTDIR)$(usrlibdir)/$(LIBS)
	$(Q)$(RM) $(DESTDIR)$(includedir)/$(HEADERS)
	$(Q)$(RM) $(DESTDIR)$(usrlibdir)/$(DEVLIB)
	@for file in $(DESTDIR)$(mandir)/man3/dmmp_*; do \
		$(RM) $$file; \
	done
	$(Q)$(RM) $(DESTDIR)$(mandir)/man3/libdmmp.h*
	$(Q)$(RM) $(DESTDIR)$(pkgconfdir)/$(PKGFILE)

clean: dep_clean
	$(Q)$(RM) core *.a *.o *.so *.so.* *.abi $(NV_VERSION_SCRIPT)
	@$(MAKE) -C test clean

include $(wildcard $(OBJS:.o=.d))

check: all
	@$(MAKE) -C test check

speed_test: all
	@$(MAKE) -C test speed_test

doc: docs/man/dmmp_strerror.3

docs/man/dmmp_strerror.3:	$(HEADERS)
	$(Q)TEMPFILE=$(shell mktemp); \
	cat $^ | perl docs/doc-preclean.pl >$$TEMPFILE; \
	LC_ALL=C \
	KBUILD_BUILD_TIMESTAMP=`git log -n1 --pretty=%cd --date=iso -- $^` \
	perl docs/kernel-doc -man $$TEMPFILE | \
	    perl docs/split-man.pl docs/man; \
	$(RM) -f $$TEMPFILE

dep_clean:
	$(Q)$(RM) $(OBJS:.o=.d)
