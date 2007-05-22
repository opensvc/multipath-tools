# Makefile
#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@free.fr>
BUILD = glibc

include ../Makefile.inc

OBJS = devmap_name.o

ifeq ($(strip $(BUILD)),klibc)
	OBJS += $(libdm)
else
	LDFLAGS = -ldevmapper
endif

EXEC = devmap_name

all: $(BUILD)

prepare:
	rm -f core *.o *.gz

glibc: prepare $(OBJS)
	$(CC) $(OBJS) -o $(EXEC) $(LDFLAGS)
	$(GZIP) $(EXEC).8 > $(EXEC).8.gz

klibc: prepare $(OBJS)
	$(CC) -static -o $(EXEC) $(OBJS)
	$(GZIP) $(EXEC).8 > $(EXEC).8.gz

install: $(EXEC) $(EXEC).8
	install -d $(DESTDIR)$(bindir)
	install -m 755 $(EXEC) $(DESTDIR)$(bindir)/
	install -d $(DESTDIR)$(mandir)
	install -m 644 $(EXEC).8.gz $(DESTDIR)$(mandir)

uninstall:
	rm $(DESTDIR)$(bindir)/$(EXEC)
	rm $(DESTDIR)$(mandir)/$(EXEC).8.gz

clean:
	rm -f core *.o $(EXEC) *.gz
