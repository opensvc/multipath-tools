EXEC		= mpath_prio_hds_modular
BUILD		= glibc
OBJS		= pp_hds_modular.o

TOPDIR		= ../..
include $(TOPDIR)/Makefile.inc

all: $(BUILD)

glibc:	$(OBJS)
	$(CC) -o $(EXEC) $(OBJS) $(LDFLAGS)

klibc:	$(OBJS)
	$(CC) -static -o $(EXEC) $(OBJS)

install: $(EXEC)
	$(INSTALL_PROGRAM) -m 755 $(EXEC) $(DESTDIR)$(bindir)/$(EXEC)

uninstall:
	rm $(DESTDIR)$(bindir)/$(EXEC)
clean:	
	rm -f *.o $(EXEC)
