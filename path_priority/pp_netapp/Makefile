EXEC		= mpath_prio_netapp
BUILD		= glibc
OBJS		= pp_netapp.o

TOPDIR		= ../..
include $(TOPDIR)/Makefile.inc

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
