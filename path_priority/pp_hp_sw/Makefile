EXEC		= mpath_prio_hp_sw
BUILD		= glibc
OBJS		= pp_hp_sw.o

TOPDIR		= ../..
include $(TOPDIR)/Makefile.inc

all: $(BUILD)

glibc:	$(OBJS)
	$(CC) -o $(EXEC) $(OBJS) $(LDFLAGS)

klibc:	$(OBJS)
	$(CC) -static -o $(EXEC) $(OBJS)

install: $(EXEC)
	install -m 755 $(EXEC) $(DESTDIR)$(bindir)/$(EXEC)

uninstall:
	rm $(DESTDIR)$(bindir)/$(EXEC)
clean:	
	rm -f *.o $(EXEC)

%.o:	%.c
	$(CC) $(CFLAGS) -c -o $@ $<
