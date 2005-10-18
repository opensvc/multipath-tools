#==============================================================================
# (C) Copyright IBM Corp. 2004, 2005   All Rights Reserved.
#
# Makefile
#
# Tool to make use of a SCSI-feature called Asymmetric Logical Unit Access.
# It determines the ALUA state of a device and prints a priority value to
# stdout.
#
# Author(s): Jan Kunigk
#            S. Bader <shbader@de.ibm.com>
#
# This file is released under the GPL.
#==============================================================================
EXEC		= mpath_prio_alua
BUILD		= glibc
DEBUG		= 0
DEBUG_DUMPHEX	= 0
OBJS		= main.o rtpg.o

TOPDIR	= ../..

ifneq ($(shell ls $(TOPDIR)/Makefile.inc 2>/dev/null),)
include $(TOPDIR)/Makefile.inc
endif

CFLAGS = -pipe -g -O2 -Wall -Wunused -Wstrict-prototypes -DDEBUG=$(DEBUG)

all: $(BUILD)

glibc:	$(OBJS)
	$(CC) -o $(EXEC) $(OBJS) $(LDFLAGS)

klibc:	$(OBJS)
	$(CC) -static -o $(EXEC) $(OBJS)

install: $(BUILD) $(EXEC).8.gz
	install -s -m 755 $(EXEC) $(DESTDIR)$(bindir)/$(EXEC)
	install -m 644 $(EXEC).8.gz $(DESTDIR)$(mandir)

uninstall:
	rm $(DESTDIR)$(bindir)/$(EXEC)
	rm $(DESTDIR)$(mandir)/$(EXEC).8.gz

clean:	
	rm -f *.o *.gz $(EXEC)

%.o:	%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(EXEC).8.gz:	$(EXEC).8
	$(GZIP) $< >$@

main.o:	main.c rtpg.h spc3.h

rtpg.o:	rtpg.c rtpg.h spc3.h
