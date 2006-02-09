# Makefile
#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@free.fr>
#
BUILD=glibc

include ../Makefile.inc

CFLAGS += -I. -D_LARGEFILE64_SOURCE

ifeq ($(strip $(BUILD)),klibc)
	OBJS = bsd.o dos.o kpartx.o solaris.o unixware.o gpt.o crc32.o \
	       lopart.o xstrncpy.o devmapper.o dasd.o mac.o \
	       $(MULTIPATHLIB)-$(BUILD).a $(libdm)
else
	LDFLAGS = -ldevmapper
	OBJS = bsd.o dos.o kpartx.o solaris.o unixware.o dasd.o \
	       gpt.o mac.o crc32.o lopart.o xstrncpy.o devmapper.o
endif

EXEC = kpartx

all: $(BUILD)

prepare:
	rm -f core *.o *.gz

glibc: prepare $(OBJS)
	$(CC) $(OBJS) -o $(EXEC) $(LDFLAGS)
	$(GZIP) $(EXEC).8 > $(EXEC).8.gz
	
klibc: prepare $(OBJS)
	$(CC) -static -o $(EXEC) $(CRT0) $(OBJS) $(KLIBC) $(LIBGCC)
	$(GZIP) $(EXEC).8 > $(EXEC).8.gz

$(MULTIPATHLIB)-$(BUILD).a:
	make -C $(multipathdir) BUILD=$(BUILD)

install:
	install -d $(DESTDIR)$(bindir)
	install -s -m 755 $(EXEC) $(DESTDIR)$(bindir)
	install -d $(DESTDIR)$(mandir)
	install -m 644 $(EXEC).8.gz $(DESTDIR)$(mandir)

uninstall:
	rm -f $(DESTDIR)$(bindir)/$(EXEC)
	rm -f $(DESTDIR)$(mandir)/$(EXEC).8.gz

clean:
	rm -f core *.o $(EXEC) *.gz
