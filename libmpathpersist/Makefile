include ../Makefile.inc

DEVLIB := libmpathpersist.so
CFLAGS += $(LIB_CFLAGS) -I$(multipathdir) -I$(mpathutildir) -I$(mpathpersistdir) -I$(mpathcmddir)
LDFLAGS += -L$(multipathdir) -L$(mpathutildir) -L$(mpathcmddir)
LIBDEPS += -lmultipath -lmpathutil -lmpathcmd -ldevmapper -lpthread -ldl

OBJS := mpath_persist.o mpath_updatepr.o mpath_pr_ioctl.o mpath_persist_int.o

all: $(DEVLIB)

include $(TOPDIR)/rules.mk

install: all
	$(Q)$(INSTALL_PROGRAM) -d $(DESTDIR)$(syslibdir)
	$(Q)$(INSTALL_PROGRAM) -m 755 $(LIBS) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(Q)$(INSTALL_PROGRAM) -m 755 -d $(DESTDIR)$(syslibdir)
	$(Q)$(INSTALL_PROGRAM) -m 755 -d $(DESTDIR)$(mandir)/man3
	$(Q)$(INSTALL_PROGRAM) -m 755 -d $(DESTDIR)$(includedir)
	$(Q)$(LN) $(LIBS) $(DESTDIR)$(syslibdir)/$(DEVLIB)
	$(Q)$(INSTALL_PROGRAM) -m 644 mpath_persistent_reserve_in.3 $(DESTDIR)$(mandir)/man3
	$(Q)$(INSTALL_PROGRAM) -m 644 mpath_persistent_reserve_out.3 $(DESTDIR)$(mandir)/man3
	$(Q)$(INSTALL_PROGRAM) -m 644 mpath_persist.h $(DESTDIR)$(includedir)

uninstall:
	$(Q)$(RM) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(Q)$(RM) $(DESTDIR)$(mandir)/man3/mpath_persistent_reserve_in.3
	$(Q)$(RM) $(DESTDIR)$(mandir)/man3/mpath_persistent_reserve_out.3
	$(Q)$(RM) $(DESTDIR)$(includedir)/mpath_persist.h
	$(Q)$(RM) $(DESTDIR)$(syslibdir)/$(DEVLIB)

clean: dep_clean
	$(Q)$(RM) core *.a *.o *.so *.so.* *.abi $(NV_VERSION_SCRIPT)

include $(wildcard $(OBJS:.o=.d))


dep_clean:
	$(Q)$(RM) $(OBJS:.o=.d)
