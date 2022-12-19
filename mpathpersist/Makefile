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
	$(Q)$(CC) $(OBJS) -o $(EXEC) $(LDFLAGS) $(CFLAGS) $(LIBDEPS)

install:
	$(Q)$(INSTALL_PROGRAM) -d $(DESTDIR)$(bindir)
	$(Q)$(INSTALL_PROGRAM) -m 755 $(EXEC) $(DESTDIR)$(bindir)/
	$(Q)$(INSTALL_PROGRAM) -d $(DESTDIR)$(mandir)/man8
	$(Q)$(INSTALL_PROGRAM) -m 644 $(EXEC).8 $(DESTDIR)$(mandir)/man8

clean: dep_clean
	$(Q)$(RM) core *.o $(EXEC)

include $(wildcard $(OBJS:.o=.d))

uninstall:
	$(Q)$(RM) $(DESTDIR)$(bindir)/$(EXEC)
	$(Q)$(RM) $(DESTDIR)$(mandir)/man8/$(EXEC).8

dep_clean:
	$(Q)$(RM) $(OBJS:.o=.d)
