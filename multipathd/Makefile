include ../Makefile.inc

#
# debuging stuff
#
#CFLAGS += -DLCKDBG
#CFLAGS += -D_DEBUG_
#CFLAGS += -DLOGDBG
CFLAGS += -I$(multipathdir) -I$(mpathpersistdir) -I$(mpathcmddir)

LIBDEPS += -ludev -ldl -L$(multipathdir) -lmultipath -L$(mpathpersistdir) \
	   -lmpathpersist -L$(mpathcmddir) -lmpathcmd -lurcu -lpthread \
	   -ldevmapper -lreadline

ifdef SYSTEMD
	CFLAGS += -DUSE_SYSTEMD=$(SYSTEMD)
	ifeq ($(shell test $(SYSTEMD) -gt 209 && echo 1), 1)
		LIBDEPS += -lsystemd
	else
		LIBDEPS += -lsystemd-daemon
	endif
endif

OBJS = main.o pidfile.o uxlsnr.o uxclnt.o cli.o cli_handlers.o

EXEC = multipathd

all : $(EXEC)

$(EXEC): $(OBJS)
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
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(mandir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).8.gz $(DESTDIR)$(mandir)

uninstall:
	$(RM) $(DESTDIR)$(bindir)/$(EXEC)
	$(RM) $(DESTDIR)$(mandir)/$(EXEC).8.gz
	$(RM) $(DESTDIR)$(unitdir)/$(EXEC).service
	$(RM) $(DESTDIR)$(unitdir)/$(EXEC).socket

clean:
	$(RM) core *.o $(EXEC) *.gz
