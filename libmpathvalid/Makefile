include ../Makefile.inc

DEVLIB := libmpathvalid.so
CPPFLAGS += -I$(multipathdir) -I$(mpathutildir) -I$(mpathcmddir)
CFLAGS += $(LIB_CFLAGS)
LIBDEPS += -lpthread -ldevmapper -ldl -L$(multipathdir) -lmultipath \
	   -L$(mpathutildir) -lmpathutil -L$(mpathcmddir) -lmpathcmd -ludev

OBJS := mpath_valid.o

all: $(LIBS)

$(LIBS): $(OBJS) $(VERSION_SCRIPT)
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -Wl,-soname=$@ -o $@ $(OBJS) $(LIBDEPS) \
		-Wl,--version-script=$(VERSION_SCRIPT)
	$(LN) $(LIBS) $(DEVLIB)

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
