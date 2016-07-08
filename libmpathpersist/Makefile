# Makefile
#
BUILD = glibc
include ../Makefile.inc

INSTALL_PROGRAM = install

SONAME=0
DEVLIB = libmpathpersist.so
LIBS = $(DEVLIB).$(SONAME)


CFLAGS += -I$(multipathdir) -I$(mpathpersistdir) -I$(mpathcmddir)
LIBDEPS +=  -lpthread -ldevmapper -ldl -L$(multipathdir) -lmultipath \
	-L$(mpathcmddir) -lmpathcmd

OBJS = mpath_persist.o mpath_updatepr.o mpath_pr_ioctl.o

all: $(LIBS)


$(LIBS):
	$(CC) -Wall -fPIC -c $(CFLAGS) *.c
	$(CC)  -shared $(LIBDEPS) -Wl,-soname=$@ $(CFLAGS) -o $@ $(OBJS)
	$(LN) $(LIBS) $(DEVLIB)
	$(GZIP) mpath_persistent_reserve_in.3 > mpath_persistent_reserve_in.3.gz
	$(GZIP) mpath_persistent_reserve_out.3 > mpath_persistent_reserve_out.3.gz

install: $(LIBS)
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(syslibdir)
	$(INSTALL_PROGRAM) -m 755 $(LIBS) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(INSTALL_PROGRAM) -m 755 -d $(DESTDIR)$(syslibdir)
	$(INSTALL_PROGRAM) -m 755 -d $(DESTDIR)$(man3dir)
	$(INSTALL_PROGRAM) -m 755 -d $(DESTDIR)$(incdir)
	$(LN) $(LIBS) $(DESTDIR)$(syslibdir)/$(DEVLIB)
	$(INSTALL_PROGRAM) -m 644 mpath_persistent_reserve_in.3.gz $(DESTDIR)$(man3dir)
	$(INSTALL_PROGRAM) -m 644 mpath_persistent_reserve_out.3.gz $(DESTDIR)$(man3dir)
	$(INSTALL_PROGRAM) -m 644 mpath_persist.h $(DESTDIR)$(incdir)

uninstall:
	$(RM) $(DESTDIR)$(syslibdir)/$(LIBS)
	$(RM) $(DESTDIR)$(man3dir)/mpath_persistent_reserve_in.3.gz
	$(RM) $(DESTDIR)$(man3dir)/mpath_persistent_reserve_out.3.gz
	$(RM) $(DESTDIR)$(incdir)/mpath_persist.h
	$(RM) $(DESTDIR)$(syslibdir)/$(DEVLIB)

clean:
	$(RM) core *.a *.o
	$(RM) libmpathpersist.so.0
	$(RM) libmpathpersist.so
	$(RM) mpath_persistent_reserve_in.3.gz mpath_persistent_reserve_out.3.gz
