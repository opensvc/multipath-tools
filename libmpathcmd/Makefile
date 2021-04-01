include ../Makefile.inc

SONAME = 0
DEVLIB = libmpathcmd.so
LIBS = $(DEVLIB).$(SONAME)
VERSION_SCRIPT := libmpathcmd.version

CFLAGS += $(LIB_CFLAGS)

OBJS = mpath_cmd.o

all:	$(DEVLIB)

$(LIBS): $(OBJS) $(VERSION_SCRIPT)
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -Wl,-soname=$@ \
		-Wl,--version-script=$(VERSION_SCRIPT) -o $@ $(OBJS) $(LIBDEPS)

$(DEVLIB): $(LIBS)
	$(LN) $(LIBS) $@

install: all
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(syslibdir)
	$(INSTALL_PROGRAM) -m 755 $(LIBS) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(LN) $(LIBS) $(DESTDIR)$(syslibdir)/$(DEVLIB)
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(includedir)
	$(INSTALL_PROGRAM) -m 644 mpath_cmd.h $(DESTDIR)$(includedir)

uninstall:
	$(RM) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(RM) $(DESTDIR)$(syslibdir)/$(DEVLIB)
	$(RM) $(DESTDIR)$(includedir)/mpath_cmd.h

clean: dep_clean
	$(RM) core *.a *.o *.so *.so.* *.gz

include $(wildcard $(OBJS:.o=.d))


dep_clean:
	$(RM) $(OBJS:.o=.d)
