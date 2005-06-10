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
else
# "out of tree building"
STRIP		= strip --strip-all -R .comment -R .note
endif

CFLAGS = -pipe -g -O2 -Wall -Wunused -Wstrict-prototypes -DDEBUG=$(DEBUG)

all: $(BUILD)

glibc:	$(OBJS)
	$(CC) -o $(EXEC) $(OBJS) $(LDFLAGS)
	$(STRIP) $(EXEC)

klibc:	$(OBJS)
	$(CC) -static -o $(EXEC) $(OBJS)
	$(STRIP) $(EXEC)

install: $(EXEC)
	install -m 755 $(EXEC) $(DESTDIR)$(bindir)/$(EXEC)

uninstall:
	rm $(DESTDIR)$(bindir)/$(EXEC)
clean:	
	rm -f *.o $(EXEC)

%.o:	%.c
	$(CC) $(CFLAGS) -c -o $@ $<

main.o:	main.c rtpg.h spc3.h

rtpg.o:	rtpg.c rtpg.h spc3.h
