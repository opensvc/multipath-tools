include ../Makefile.inc

#
# debugging stuff
#
#CFLAGS += -DLCKDBG
#CFLAGS += -D_DEBUG_
#CFLAGS += -DLOGDBG
CFLAGS += $(BIN_CFLAGS) -I$(multipathdir) -I$(mpathpersistdir) \
	  -I$(mpathcmddir) -I$(thirdpartydir)
LDFLAGS += $(BIN_LDFLAGS)
LIBDEPS += -L$(multipathdir) -lmultipath -L$(mpathpersistdir) -lmpathpersist \
	   -L$(mpathcmddir) -lmpathcmd -ludev -ldl -lurcu -lpthread \
	   -ldevmapper -lreadline

ifdef SYSTEMD
	CFLAGS += -DUSE_SYSTEMD=$(SYSTEMD)
	ifeq ($(shell test $(SYSTEMD) -gt 209 && echo 1), 1)
		LIBDEPS += -lsystemd
	else
		LIBDEPS += -lsystemd-daemon
	endif
endif
ifeq ($(ENABLE_DMEVENTS_POLL),0)
	CFLAGS += -DNO_DMEVENTS_POLL
endif

OBJS = main.o pidfile.o uxlsnr.o uxclnt.o cli.o cli_handlers.o waiter.o \
       dmevents.o

EXEC = multipathd

all : $(EXEC)

$(EXEC): $(OBJS) $(multipathdir)/libmultipath.so $(mpathcmddir)/libmpathcmd.so
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $(EXEC) $(LIBDEPS)
	$(GZIP) $(EXEC).8 > $(EXEC).8.gz

install:
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -m 755 $(EXEC) $(DESTDIR)$(bindir)
ifdef SYSTEMD
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(unitdir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).service $(DESTDIR)$(unitdir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).socket $(DESTDIR)$(unitdir)
endif
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(man8dir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).8.gz $(DESTDIR)$(man8dir)

uninstall:
	$(RM) $(DESTDIR)$(bindir)/$(EXEC)
	$(RM) $(DESTDIR)$(man8dir)/$(EXEC).8.gz
	$(RM) $(DESTDIR)$(unitdir)/$(EXEC).service
	$(RM) $(DESTDIR)$(unitdir)/$(EXEC).socket

clean: dep_clean
	$(RM) core *.o $(EXEC) *.gz

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(RM) $(OBJS:.o=.d)
