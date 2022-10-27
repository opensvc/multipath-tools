include ../Makefile.inc

DEVLIB := libmpathcmd.so
CFLAGS += $(LIB_CFLAGS)
OBJS := mpath_cmd.o

all:	$(DEVLIB)

include $(TOPDIR)/rules.mk

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
	$(RM) core *.a *.o *.so *.so.* *.abi $(NV_VERSION_SCRIPT)

include $(wildcard $(OBJS:.o=.d))


dep_clean:
	$(RM) $(OBJS:.o=.d)
