include ../Makefile.inc

EXEC     := multipathd
CLI      := multipathc
MANPAGES := multipathd.8

CPPFLAGS += -I$(multipathdir) -I$(mpathutildir) -I$(mpathpersistdir) -I$(mpathcmddir) -I$(thirdpartydir) \
	-DBINDIR='"$(bindir)"' $(SYSTEMD_CPPFLAGS)

#
# debugging stuff
#
#CPPFLAGS += -DLCKDBG
#CPPFLAGS += -DLOGDBG

CFLAGS += $(BIN_CFLAGS)
LDFLAGS += $(BIN_LDFLAGS)

CLI_LIBDEPS	:=  -L$(mpathutildir) -lmpathutil -L$(mpathcmddir) -lmpathcmd \
		-ludev -ldl -lurcu -lpthread $(SYSTEMD_LIBDEPS)
LIBDEPS		:= -L$(multipathdir) -lmultipath -L$(mpathpersistdir) -lmpathpersist \
	  	-ldevmapper $(CLI_LIBDEPS)

ifeq ($(READLINE),libedit)
RL_CPPFLAGS	:= -DUSE_LIBEDIT
RL_LIBDEPS	:= -ledit
# See comment in multipathc.c
ifeq ($(shell sed -En 's/.*\<Function\s*\*rl_completion_entry_function;.*/yes/p' /usr/include/editline/readline.h),yes)
RL_CPPFLAGS += -DBROKEN_RL_COMPLETION_FUNC
endif
endif
ifeq ($(READLINE),libreadline)
RL_CPPFLAGS	:= -DUSE_LIBREADLINE
RL_LIBDEPS	:= -lreadline
endif

CLI_OBJS := multipathc.o cli.o
OBJS := main.o pidfile.o uxlsnr.o uxclnt.o cli.o cli_handlers.o waiter.o \
       dmevents.o init_unwinder.o
ifeq ($(FPIN_SUPPORT),1)
OBJS += fpin_handlers.o
endif

all : $(EXEC) $(CLI) $(MANPAGES) $(EXEC).service

$(EXEC): $(OBJS) $(multipathdir)/libmultipath.so $(mpathcmddir)/libmpathcmd.so
	@echo building $@ because of $?
	$(Q)$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $(EXEC) $(LIBDEPS)

multipathc.o:	multipathc.c
	$(Q)$(CC) $(CPPFLAGS) $(RL_CPPFLAGS) $(CFLAGS) -Wno-unused-parameter -c -o $@ $<

$(CLI):  $(CLI_OBJS)
	@echo building $@ because of $?
	$(Q)$(CC) $(CFLAGS) $(CLI_OBJS) $(LDFLAGS) -o $@ $(CLI_LIBDEPS) $(RL_LIBDEPS)

cli_handlers.o:	cli_handlers.c
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-unused-parameter -c -o $@ $<

main.o:	main.c
	$(Q)$(CC) $(CPPFLAGS) -DSCHED_RT_PRIO=$(SCHED_RT_PRIO) $(CFLAGS) -c -o $@ $<

install:
	$(Q)$(INSTALL_PROGRAM) -d $(DESTDIR)$(bindir)
	$(Q)$(INSTALL_PROGRAM) -m 755 $(EXEC) $(DESTDIR)$(bindir)
	$(Q)$(INSTALL_PROGRAM) -m 755 $(CLI) $(DESTDIR)$(bindir)
ifdef SYSTEMD
	$(Q)$(INSTALL_PROGRAM) -d $(DESTDIR)$(unitdir)
	$(Q)$(INSTALL_PROGRAM) -m 644 $(EXEC).service $(DESTDIR)$(unitdir)
	$(Q)$(INSTALL_PROGRAM) -m 644 $(EXEC).socket $(DESTDIR)$(unitdir)
endif
	$(Q)$(INSTALL_PROGRAM) -d $(DESTDIR)$(mandir)/man8
	$(Q)$(INSTALL_PROGRAM) -m 644 $(EXEC).8 $(DESTDIR)$(mandir)/man8
	$(Q)$(INSTALL_PROGRAM) -m 644 $(CLI).8 $(DESTDIR)$(mandir)/man8

uninstall:
	$(Q)$(RM) $(DESTDIR)$(bindir)/$(EXEC) $(DESTDIR)$(bindir)/$(CLI)
	$(Q)$(RM) $(DESTDIR)$(mandir)/man8/$(EXEC).8
	$(Q)$(RM) $(DESTDIR)$(mandir)/man8/$(CLI).8
	$(Q)$(RM) $(DESTDIR)$(unitdir)/$(EXEC).service
	$(Q)$(RM) $(DESTDIR)$(unitdir)/$(EXEC).socket

clean: dep_clean
	$(Q)$(RM) core *.o $(EXEC) $(CLI) $(MANPAGES) $(EXEC).service

include $(wildcard $(OBJS:.o=.d) $(CLI_OBJS:.o=.d))

dep_clean:
	$(Q)$(RM) $(OBJS:.o=.d) $(CLI_OBJS:.o=.d)
