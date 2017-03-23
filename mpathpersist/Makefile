include ../Makefile.inc

CFLAGS += -I$(multipathdir) -I$(mpathpersistdir)

LIBDEPS += -lpthread -ldevmapper -L$(mpathpersistdir) -lmpathpersist \
	   -L$(multipathdir) -L$(mpathcmddir) -lmpathcmd -lmultipath -ludev

EXEC = mpathpersist

OBJS = main.o

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(OBJS) -o $(EXEC) $(LDFLAGS) $(CFLAGS) $(LIBDEPS)
	$(GZIP) $(EXEC).8 > $(EXEC).8.gz

install:
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -m 755 $(EXEC) $(DESTDIR)$(bindir)/
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(man8dir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).8.gz $(DESTDIR)$(man8dir)

clean:
	$(RM) core *.o $(EXEC) *.gz

uninstall:
	$(RM) $(DESTDIR)$(bindir)/$(EXEC)
	$(RM) $(DESTDIR)$(man8dir)/$(EXEC).8.gz
