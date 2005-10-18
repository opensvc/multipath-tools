EXEC		= mpath_prio_random
BUILD		= glibc
OBJS		= pp_random.o

TOPDIR		= ../..
include $(TOPDIR)/Makefile.inc

CFLAGS = -pipe -g -O2 -Wall -Wunused -Wstrict-prototypes

all: $(BUILD)

glibc:	$(OBJS)
	$(CC) -o $(EXEC) $(OBJS) $(LDFLAGS)

klibc:	$(OBJS)
	$(CC) -static -o $(EXEC) $(OBJS)

install: $(EXEC)
	install -s -m 755 $(EXEC) $(DESTDIR)$(bindir)/$(EXEC)

uninstall:
	rm $(DESTDIR)$(bindir)/$(EXEC)
clean:	
	rm -f *.o $(EXEC)

%.o:	%.c
	$(CC) $(CFLAGS) -c -o $@ $<
