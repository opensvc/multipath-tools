include ../Makefile.inc

SONAME = 0
DEVLIB = libmpathpersist.so
LIBS = $(DEVLIB).$(SONAME)
VERSION_SCRIPT := libmpathpersist.version

CFLAGS += $(LIB_CFLAGS) -I$(multipathdir) -I$(mpathpersistdir) -I$(mpathcmddir)
LDFLAGS += -L$(multipathdir) -L$(mpathcmddir)

LIBDEPS += -lmultipath -lmpathcmd -ldevmapper -lpthread -ldl

OBJS = mpath_persist.o mpath_updatepr.o mpath_pr_ioctl.o

all: $(DEVLIB) man

$(LIBS): $(OBJS) $(VERSION_SCRIPT)
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -Wl,-soname=$@ \
		-Wl,--version-script=$(VERSION_SCRIPT) -o $@ $(OBJS) $(LIBDEPS)

$(DEVLIB): $(LIBS)
	$(LN) $(LIBS) $@

man:
	$(GZIP) mpath_persistent_reserve_in.3 > mpath_persistent_reserve_in.3.gz
	$(GZIP) mpath_persistent_reserve_out.3 > mpath_persistent_reserve_out.3.gz

install: all
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(syslibdir)
	$(INSTALL_PROGRAM) -m 755 $(LIBS) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(INSTALL_PROGRAM) -m 755 -d $(DESTDIR)$(syslibdir)
	$(INSTALL_PROGRAM) -m 755 -d $(DESTDIR)$(man3dir)
	$(INSTALL_PROGRAM) -m 755 -d $(DESTDIR)$(includedir)
	$(LN) $(LIBS) $(DESTDIR)$(syslibdir)/$(DEVLIB)
	$(INSTALL_PROGRAM) -m 644 mpath_persistent_reserve_in.3.gz $(DESTDIR)$(man3dir)
	$(INSTALL_PROGRAM) -m 644 mpath_persistent_reserve_out.3.gz $(DESTDIR)$(man3dir)
	$(INSTALL_PROGRAM) -m 644 mpath_persist.h $(DESTDIR)$(includedir)

uninstall:
	$(RM) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(RM) $(DESTDIR)$(man3dir)/mpath_persistent_reserve_in.3.gz
	$(RM) $(DESTDIR)$(man3dir)/mpath_persistent_reserve_out.3.gz
	$(RM) $(DESTDIR)$(includedir)/mpath_persist.h
	$(RM) $(DESTDIR)$(syslibdir)/$(DEVLIB)

clean: dep_clean
	$(RM) core *.a *.o *.so *.so.* *.gz

include $(wildcard $(OBJS:.o=.d))


dep_clean:
	$(RM) $(OBJS:.o=.d)
