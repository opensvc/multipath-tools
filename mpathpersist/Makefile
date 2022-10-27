include ../Makefile.inc

CPPFLAGS += -I$(multipathdir) -I$(mpathutildir) -I$(mpathpersistdir)
CFLAGS += $(BIN_CFLAGS)
LDFLAGS += $(BIN_LDFLAGS)

LIBDEPS += -L$(mpathpersistdir) -lmpathpersist -L$(multipathdir) -lmultipath \
	-L$(mpathutildir) -lmpathutil -L$(mpathcmddir) -lmpathcmd -lpthread -ldevmapper -ludev

EXEC = mpathpersist

OBJS = main.o

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(OBJS) -o $(EXEC) $(LDFLAGS) $(CFLAGS) $(LIBDEPS)

install:
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -m 755 $(EXEC) $(DESTDIR)$(bindir)/
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(mandir)/man8
	$(INSTALL_PROGRAM) -m 644 $(EXEC).8 $(DESTDIR)$(mandir)/man8

clean: dep_clean
	$(RM) core *.o $(EXEC)

include $(wildcard $(OBJS:.o=.d))

uninstall:
	$(RM) $(DESTDIR)$(bindir)/$(EXEC)
	$(RM) $(DESTDIR)$(mandir)/man8/$(EXEC).8

dep_clean:
	$(RM) $(OBJS:.o=.d)
