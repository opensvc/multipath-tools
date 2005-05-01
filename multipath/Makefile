# Makefile
#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@free.fr>
BUILD = glibc

include ../Makefile.inc

OBJS = main.o $(MULTIPATHLIB)-$(BUILD).a $(CHECKERSLIB)-$(BUILD).a

CFLAGS = -pipe -g -Wall -Wunused -Wstrict-prototypes \
	 -I$(multipathdir) -I$(checkersdir)

ifeq ($(strip $(BUILD)),klibc)
	OBJS += $(libdm) $(libsysfs)
else
	LDFLAGS += -ldevmapper -lsysfs
endif

EXEC = multipath

all: $(BUILD)

prepare:
	make -C $(multipathdir) clean
	rm -f core *.o *.gz

glibc: prepare $(OBJS)
	$(CC) $(OBJS) -o $(EXEC) $(LDFLAGS)
	$(STRIP) $(EXEC)
	$(GZIP) $(EXEC).8 > $(EXEC).8.gz
	
klibc: prepare $(OBJS)
	$(CC) -static -o $(EXEC) $(CRT0) $(OBJS) $(KLIBC) $(LIBGCC)
	$(STRIP) $(EXEC)
	$(GZIP) $(EXEC).8 > $(EXEC).8.gz

$(CHECKERSLIB)-$(BUILD).a:
	make -C $(checkersdir) BUILD=$(BUILD) $(BUILD)

$(MULTIPATHLIB)-$(BUILD).a:
	make -C $(multipathdir) BUILD=$(BUILD) $(BUILD)

install:
	install -d $(DESTDIR)$(bindir)
	install -m 755 $(EXEC) $(DESTDIR)$(bindir)/
	install -d $(DESTDIR)/var/cache/multipath/
	install -d $(DESTDIR)/etc/dev.d/block/
	install -m 755 multipath.dev $(DESTDIR)/etc/dev.d/block/
	install -d $(DESTDIR)/etc/udev/rules.d
	install -m 755 multipath.rules $(DESTDIR)/etc/udev/rules.d/
	install -d $(DESTDIR)$(mandir)
	install -m 644 $(EXEC).8.gz $(DESTDIR)$(mandir)

uninstall:
	rm $(DESTDIR)/etc/dev.d/block/multipath.dev
	rm $(DESTDIR)/etc/udev/rules.d/multipath.rules
	rm $(DESTDIR)$(bindir)/$(EXEC)
	rm $(DESTDIR)$(mandir)/$(EXEC).8.gz

clean:
	rm -f core *.o $(EXEC) *.gz
