# Makefile
#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@free.fr>
BUILD = glibc

include ../Makefile.inc

OBJS = main.o $(MULTIPATHLIB)-$(BUILD).a $(CHECKERSLIB)-$(BUILD).a

CFLAGS += -I$(multipathdir) -I$(checkersdir)
LDFLAGS += -laio

ifeq ($(strip $(BUILD)),klibc)
	OBJS += $(libdm)
else
	LDFLAGS += -ldevmapper
endif

EXEC = multipath

all: $(BUILD)

prepare:
	make -C $(multipathdir) prepare
	rm -f core *.o *.gz
	$(GZIP) $(EXEC).8 > $(EXEC).8.gz
	$(GZIP) $(EXEC).conf.5 > $(EXEC).conf.5.gz

glibc: prepare $(OBJS)
	$(CC) $(OBJS) -o $(EXEC) $(LDFLAGS)

klibc: prepare $(OBJS)
	$(CC) -static -o $(EXEC) $(CRT0) $(OBJS) $(KLIBC) $(LIBGCC)

$(CHECKERSLIB)-$(BUILD).a:
	make -C $(checkersdir) BUILD=$(BUILD) $(BUILD)

$(MULTIPATHLIB)-$(BUILD).a:
	make -C $(multipathdir) BUILD=$(BUILD) $(BUILD)

install:
	install -d $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -m 755 $(EXEC) $(DESTDIR)$(bindir)/
	install -d $(DESTDIR)/etc/udev/rules.d
	install -m 644 multipath.rules $(DESTDIR)/etc/udev/rules.d/
	install -d $(DESTDIR)$(mandir)
	install -m 644 $(EXEC).8.gz $(DESTDIR)$(mandir)
	install -d $(DESTDIR)$(man5dir)
	install -m 644 $(EXEC).conf.5.gz $(DESTDIR)$(man5dir)

uninstall:
	rm $(DESTDIR)/etc/udev/rules.d/multipath.rules
	rm $(DESTDIR)$(bindir)/$(EXEC)
	rm $(DESTDIR)$(mandir)/$(EXEC).8.gz
	rm $(DESTDIR)$(man5dir)/$(EXEC).conf.5.gz

clean:
	rm -f core *.o $(EXEC) *.gz
