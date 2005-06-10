EXEC		= mpath_prio_emc
BUILD		= glibc
OBJS		= pp_emc.o

TOPDIR		= ../..
include $(TOPDIR)/Makefile.inc

CFLAGS = -pipe -g -O2 -Wall -Wunused -Wstrict-prototypes

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
