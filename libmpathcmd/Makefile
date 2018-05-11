include ../Makefile.inc

SONAME = 0
DEVLIB = libmpathcmd.so
LIBS = $(DEVLIB).$(SONAME)

CFLAGS += $(LIB_CFLAGS)

OBJS = mpath_cmd.o

all: $(LIBS)

$(LIBS): $(OBJS)
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -Wl,-soname=$@ -o $@ $(OBJS) $(LIBDEPS)
	$(LN) $@ $(DEVLIB)

install: $(LIBS)
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
