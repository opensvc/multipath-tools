#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#
include ../Makefile.inc

CFLAGS += $(BIN_CFLAGS) -I$(multipathdir) -I$(mpathcmddir)
LDFLAGS += $(BIN_LDFLAGS)
LIBDEPS += -lpthread -ldevmapper -ldl -L$(multipathdir) -lmultipath -ludev \
	   -L$(mpathcmddir) -lmpathcmd

EXEC = multipath

OBJS = main.o

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(EXEC) $(LDFLAGS) $(LIBDEPS)
	$(GZIP) $(EXEC).8 > $(EXEC).8.gz
	$(GZIP) $(EXEC).conf.5 > $(EXEC).conf.5.gz

install:
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -m 755 $(EXEC) $(DESTDIR)$(bindir)/
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(udevrulesdir)
	$(INSTALL_PROGRAM) -m 644 11-dm-mpath.rules $(DESTDIR)$(udevrulesdir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).rules $(DESTDIR)$(libudevdir)/rules.d/56-multipath.rules
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(man8dir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).8.gz $(DESTDIR)$(man8dir)
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(man5dir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).conf.5.gz $(DESTDIR)$(man5dir)

uninstall:
	$(RM) $(DESTDIR)$(bindir)/$(EXEC)
	$(RM) $(DESTDIR)$(udevrulesdir)/11-dm-mpath.rules
	$(RM) $(DESTDIR)$(libudevdir)/rules.d/56-multipath.rules
	$(RM) $(DESTDIR)$(man8dir)/$(EXEC).8.gz
	$(RM) $(DESTDIR)$(man5dir)/$(EXEC).conf.5.gz

clean:
	$(RM) core *.o $(EXEC) *.gz
