include ../Makefile.inc

SONAME = 0
DEVLIB = libmpathvalid.so
LIBS = $(DEVLIB).$(SONAME)
VERSION_SCRIPT := libmpathvalid.version

CFLAGS += $(LIB_CFLAGS) -I$(multipathdir) -I$(mpathcmddir)

LIBDEPS += -lpthread -ldevmapper -ldl -L$(multipathdir) \
	   -lmultipath -L$(mpathcmddir) -lmpathcmd -ludev

OBJS = mpath_valid.o

all: $(LIBS)

$(LIBS): $(OBJS) $(VERSION_SCRIPT)
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -Wl,-soname=$@ -o $@ $(OBJS) $(LIBDEPS) -Wl,--version-script=libmpathvalid.version
	$(LN) $(LIBS) $(DEVLIB)

install: $(LIBS)
	$(INSTALL_PROGRAM) -m 755 -d $(DESTDIR)$(syslibdir)
	$(INSTALL_PROGRAM) -m 755 $(LIBS) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(LN) $(LIBS) $(DESTDIR)$(syslibdir)/$(DEVLIB)
	$(INSTALL_PROGRAM) -m 755 -d $(DESTDIR)$(includedir)
	$(INSTALL_PROGRAM) -m 644 mpath_valid.h $(DESTDIR)$(includedir)

uninstall:
	$(RM) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(RM) $(DESTDIR)$(syslibdir)/$(DEVLIB)
	$(RM) $(DESTDIR)$(includedir)/mpath_valid.h

clean: dep_clean
	$(RM) core *.a *.o *.so *.so.* *.gz

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(RM) $(OBJS:.o=.d)
