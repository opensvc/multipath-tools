include ../Makefile.inc

SONAME = 0
DEVLIB = libmpathvalid.so
LIBS = $(DEVLIB).$(SONAME)
VERSION_SCRIPT := libmpathvalid.version

CPPFLAGS += -I$(multipathdir) -I$(mpathcmddir)
CFLAGS += $(LIB_CFLAGS)

LIBDEPS += -lpthread -ldevmapper -ldl -L$(multipathdir) \
	   -lmultipath -L$(mpathcmddir) -lmpathcmd -ludev

OBJS = mpath_valid.o

all: $(LIBS)

$(LIBS): $(OBJS) $(VERSION_SCRIPT)
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -Wl,-soname=$@ -o $@ $(OBJS) $(LIBDEPS) \
		-Wl,--version-script=$(VERSION_SCRIPT)
	$(LN) $(LIBS) $(DEVLIB)

$(NV_VERSION_SCRIPT):	$(VERSION_SCRIPT)
	@printf 'NOVERSION {\nglobal:\n' >$@
	@grep -P '^[ \t]+[a-zA-Z_][a-zA-Z0-9_]*;' $< >>$@
	@printf 'local:\n\t*;\n};\n' >>$@

$(LIBS:%.so.$(SONAME)=%-nv.so):	$(OBJS) $(NV_VERSION_SCRIPT)
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -Wl,-soname=$@ \
		-Wl,--version-script=$(NV_VERSION_SCRIPT) -o $@ $(OBJS) $(LIBDEPS)

abi:    $(LIBS:%.so.$(SONAME)=%-nv.abi)

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
	$(RM) core *.a *.o *.so *.so.* *.abi $(NV_VERSION_SCRIPT)

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(RM) $(OBJS:.o=.d)
