# Makefile
#
include ../Makefile.inc

OBJS = main.o

CFLAGS += -I$(multipathdir) -I$(mpathpersistdir)
LDFLAGS += -lpthread -ldevmapper -L$(mpathpersistdir) -lmpathpersist -L$(multipathdir) -L$(mpathcmddir) -lmpathcmd -lmultipath -ludev

EXEC = mpathpersist

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) -g $(OBJS) -o $(EXEC) $(LDFLAGS) $(CFLAGS)
	$(GZIP) $(EXEC).8 > $(EXEC).8.gz
	
install:
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -m 755 $(EXEC) $(DESTDIR)$(bindir)/
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(mandir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).8.gz $(DESTDIR)$(mandir)
	
clean:
	$(RM) *.o $(EXEC)
	$(RM) mpathpersist.8.gz

uninstall:
	$(RM) $(DESTDIR)$(bindir)/$(EXEC)
	$(RM) $(DESTDIR)$(mandir)/$(EXEC).8.gz
