BUILD = glibc
EXEC = multipathd

include ../Makefile.inc

#
# directories where to put stuff
#
bindir = /usr/bin
mandir = /usr/share/man/man8
rcdir = /etc/init.d

#
# basic flags setting
#
CFLAGS = -pipe -g -Wall -Wunused -Wstrict-prototypes \
	 -DDAEMON -I$(multipathdir) -I$(checkersdir)
LDFLAGS = -lpthread -ldevmapper -lsysfs -lreadline -lcurses

#
# debuging stuff
#
#CFLAGS += -DLCKDBG
#CFLAGS += -D_DEBUG_
#CFLAGS += -DLOGDBG

#
# object files
#
OBJS = main.o log.o log_pthread.o pidfile.o uxlsnr.o uxclnt.o cli.o cli_handlers.o \
       $(MULTIPATHLIB)-glibc.a $(CHECKERSLIB)-glibc.a \


#
# directives
#
all : $(BUILD)

glibc: $(EXEC)

klibc:
	$(MAKE) BUILD=glibc glibc

$(EXEC): clean $(OBJS)
	$(CC) $(OBJS) -o $(EXEC) $(LDFLAGS)
	$(STRIP) $(EXEC)
	$(GZIP) $(EXEC).8 > $(EXEC).8.gz

$(CHECKERSLIB)-glibc.a:
	$(MAKE) -C $(checkersdir) BUILD=glibc glibc

$(MULTIPATHLIB)-glibc.a:
	$(MAKE) -C $(multipathdir) DAEMON=1 BUILD=glibc glibc

install:
	install -d $(DESTDIR)$(bindir)
	install -m 755 $(EXEC) $(DESTDIR)$(bindir)
	install -d $(DESTDIR)$(rcdir)
	install -d $(DESTDIR)$(mandir)
	install -m 644 $(EXEC).8.gz $(DESTDIR)$(mandir)

uninstall:
	rm -f $(DESTDIR)$(bindir)/$(EXEC)
	rm -f $(DESTDIR)$(rcdir)/$(EXEC)
	rm -f $(DESTDIR)$(mandir)/$(EXEC).8.gz

clean:
	$(MAKE) -C $(multipathdir) clean
	rm -f core *.o $(EXEC) *.gz

