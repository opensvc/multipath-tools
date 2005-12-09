# Makefile
#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@free.fr>
#
BUILD = glibc
DEBUG = 0

TOPDIR	= ../..
include $(TOPDIR)/Makefile.inc

ifeq ($(strip $(BUILD)),klibc)
	CFLAGS += -I/usr/include -DDEBUG=$(DEBUG)
	OBJS = pp_balance_units.o $(MULTIPATHLIB)-$(BUILD).a
else
	CFLAGS += -I$(multipathdir) -DDEBUG=$(DEBUG)
	LDFLAGS = -ldevmapper
	OBJS = pp_balance_units.o $(MULTIPATHLIB)-$(BUILD).a
endif

EXEC = mpath_prio_balance_units

all: $(BUILD)

prepare:
	rm -f core *.o *.gz

glibc: prepare $(OBJS)
	$(CC) -o $(EXEC) $(OBJS) $(LDFLAGS)

klibc: prepare $(OBJS)
	$(CC) -static -o $(EXEC) $(CRT0) $(OBJS) $(KLIBC) $(LIBGCC)

$(MULTIPATHLIB)-$(BUILD).a:
	make -C $(multipathdir) BUILD=$(BUILD) $(BUILD)

install:
	install -d $(DESTDIR)$(bindir)
	install -s -m 755 $(EXEC) $(DESTDIR)$(bindir)/

uninstall:
	rm $(DESTDIR)$(bindir)/$(EXEC)

clean:
	rm -f core *.o $(EXEC) *.gz
