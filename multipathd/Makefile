EXEC = multipathd

include ../Makefile.inc

#
# basic flags setting
#
CFLAGS += -I$(multipathdir) -I$(mpathpersistdir)
ifdef SYSTEMD
	CFLAGS += -DUSE_SYSTEMD=$(SYSTEMD)
endif
LDFLAGS += -lpthread -ldevmapper -lreadline
ifdef SYSTEMD
	LDFLAGS += -lsystemd-daemon
endif
LDFLAGS += -ludev -ldl \
	-L$(multipathdir) -lmultipath -L$(mpathpersistdir) -lmpathpersist

#
# debuging stuff
#
#CFLAGS += -DLCKDBG
#CFLAGS += -D_DEBUG_
#CFLAGS += -DLOGDBG

#
# object files
#
OBJS = main.o pidfile.o uxlsnr.o uxclnt.o cli.o cli_handlers.o


#
# directives
#
all : $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $(EXEC)
	$(GZIP) $(EXEC).8 > $(EXEC).8.gz

install:
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -m 755 $(EXEC) $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(rcdir)
ifdef SYSTEMD
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(unitdir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).service $(DESTDIR)$(unitdir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).socket $(DESTDIR)$(unitdir)
endif
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(mandir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).8.gz $(DESTDIR)$(mandir)

uninstall:
	rm -f $(DESTDIR)$(bindir)/$(EXEC)
	rm -f $(DESTDIR)$(rcdir)/$(EXEC)
	rm -f $(DESTDIR)$(mandir)/$(EXEC).8.gz
	rm -f $(DESTDIR)$(unitdir)/$(EXEC).service
	rm -f $(DESTDIR)$(unitdir)/$(EXEC).socket

clean:
	rm -f core *.o $(EXEC) *.gz

