include ../Makefile.inc

DEVLIB := libmpathvalid.so
CPPFLAGS += -I$(multipathdir) -I$(mpathutildir) -I$(mpathcmddir)
CFLAGS += $(LIB_CFLAGS)
LIBDEPS += -lpthread -ldevmapper -ldl -L$(multipathdir) -lmultipath \
	   -L$(mpathutildir) -lmpathutil -L$(mpathcmddir) -lmpathcmd -ludev

OBJS := mpath_valid.o

all: $(DEVLIB)

include $(TOPDIR)/rules.mk

install: $(LIBS)
	$(Q)$(INSTALL_PROGRAM) -m 755 -d $(DESTDIR)$(syslibdir)
	$(Q)$(INSTALL_PROGRAM) -m 755 $(LIBS) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(Q)$(LN) $(LIBS) $(DESTDIR)$(syslibdir)/$(DEVLIB)
	$(Q)$(INSTALL_PROGRAM) -m 755 -d $(DESTDIR)$(includedir)
	$(Q)$(INSTALL_PROGRAM) -m 644 mpath_valid.h $(DESTDIR)$(includedir)

uninstall:
	$(Q)$(RM) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(Q)$(RM) $(DESTDIR)$(syslibdir)/$(DEVLIB)
	$(Q)$(RM) $(DESTDIR)$(includedir)/mpath_valid.h

clean: dep_clean
	$(Q)$(RM) core *.a *.o *.so *.so.* *.abi $(NV_VERSION_SCRIPT)

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(Q)$(RM) $(OBJS:.o=.d)
