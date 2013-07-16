# Makefile
#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#
include ../Makefile.inc

OBJS = main.o

CFLAGS += -I$(multipathdir)
LDFLAGS += -lpthread -ldevmapper -ldl -L$(multipathdir) -lmultipath -ludev

EXEC = multipath

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(EXEC) $(LDFLAGS)
	$(GZIP) $(EXEC).8 > $(EXEC).8.gz
	$(GZIP) $(EXEC).conf.5 > $(EXEC).conf.5.gz

install:
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -m 755 $(EXEC) $(DESTDIR)$(bindir)/
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(mandir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).8.gz $(DESTDIR)$(mandir)
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(man5dir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).conf.5.gz $(DESTDIR)$(man5dir)

uninstall:
	rm $(DESTDIR)$(bindir)/$(EXEC)
	rm $(DESTDIR)$(mandir)/$(EXEC).8.gz
	rm $(DESTDIR)$(man5dir)/$(EXEC).conf.5.gz

clean:
	rm -f core *.o $(EXEC) *.gz
