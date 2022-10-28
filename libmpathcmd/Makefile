include ../Makefile.inc

DEVLIB := libmpathcmd.so
CFLAGS += $(LIB_CFLAGS)
OBJS := mpath_cmd.o

all:	$(DEVLIB)

include $(TOPDIR)/rules.mk

install: all
	$(Q)$(INSTALL_PROGRAM) -d $(DESTDIR)$(syslibdir)
	$(Q)$(INSTALL_PROGRAM) -m 755 $(LIBS) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(Q)$(LN) $(LIBS) $(DESTDIR)$(syslibdir)/$(DEVLIB)
	$(Q)$(INSTALL_PROGRAM) -d $(DESTDIR)$(includedir)
	$(Q)$(INSTALL_PROGRAM) -m 644 mpath_cmd.h $(DESTDIR)$(includedir)

uninstall:
	$(Q)$(RM) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(Q)$(RM) $(DESTDIR)$(syslibdir)/$(DEVLIB)
	$(Q)$(RM) $(DESTDIR)$(includedir)/mpath_cmd.h

clean: dep_clean
	$(Q)$(RM) core *.a *.o *.so *.so.* *.abi $(NV_VERSION_SCRIPT)

include $(wildcard $(OBJS:.o=.d))


dep_clean:
	$(Q)$(RM) $(OBJS:.o=.d)
