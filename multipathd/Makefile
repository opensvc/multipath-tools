include ../Makefile.inc

ifneq ($(call check_func,dm_task_get_errno,$(devmapper_incdir)/libdevmapper.h),0)
	CPPFLAGS += -DLIBDM_API_GET_ERRNO
endif

ifneq ($(call check_var,ELS_DTAG_LNK_INTEGRITY,$(kernel_incdir)/scsi/fc/fc_els.h),0)
	CPPFLAGS += -DFPIN_EVENT_HANDLER
	FPIN_SUPPORT = 1
endif
#
# debugging stuff
#
#CPPFLAGS += -DLCKDBG
#CPPFLAGS += -D_DEBUG_
#CPPFLAGS += -DLOGDBG

CPPFLAGS += -I$(multipathdir) -I$(mpathutildir) -I$(mpathpersistdir) -I$(mpathcmddir) -I$(thirdpartydir) \
	$(shell $(PKGCONFIG) --modversion liburcu 2>/dev/null | \
		awk -F. '{ printf("-DURCU_VERSION=0x%06x", 256 * ( 256 * $$1 + $$2) + $$3); }') \
	-DBINDIR='"$(bindir)"'
CFLAGS += $(BIN_CFLAGS)
LDFLAGS += $(BIN_LDFLAGS)

CLI_LIBDEPS :=  -L$(mpathutildir) -lmpathutil -L$(mpathcmddir) -lmpathcmd -ludev -ldl -lurcu -lpthread
LIBDEPS += -L$(multipathdir) -lmultipath -L$(mpathpersistdir) -lmpathpersist \
	   -ldevmapper $(CLI_LIBDEPS)


ifeq ($(READLINE),libedit)
RL_CPPFLAGS = -DUSE_LIBEDIT
RL_LIBDEPS += -ledit
endif
ifeq ($(READLINE),libreadline)
RL_CPPFLAGS += -DUSE_LIBREADLINE
RL_LIBDEPS += -lreadline
# See comment in uxclnt.c
ifeq ($(shell sed -En 's/.*\<Function\s*\*rl_completion_entry_function;.*/yes/p' /usr/include/editline/readline.h),yes)
RL_CPPFLAGS += -DBROKEN_RL_COMPLETION_FUNC
endif
endif

ifdef SYSTEMD
	CPPFLAGS += -DUSE_SYSTEMD=$(SYSTEMD)
	ifeq ($(shell test $(SYSTEMD) -gt 209 && echo 1), 1)
		CLI_LIBDEPS += -lsystemd
	else
		CLI_LIBDEPS += -lsystemd-daemon
	endif
endif
ifeq ($(ENABLE_DMEVENTS_POLL),0)
	CPPFLAGS += -DNO_DMEVENTS_POLL
endif

OBJS = main.o pidfile.o uxlsnr.o uxclnt.o cli.o cli_handlers.o waiter.o \
       dmevents.o init_unwinder.o

CLI_OBJS = multipathc.o cli.o

ifeq ($(FPIN_SUPPORT),1)
OBJS += fpin_handlers.o
endif



EXEC = multipathd
CLI = multipathc

all : $(EXEC) $(CLI)

$(EXEC): $(OBJS) $(multipathdir)/libmultipath.so $(mpathcmddir)/libmpathcmd.so
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $(EXEC) $(LIBDEPS)

multipathc.o:	multipathc.c
	$(CC) $(CPPFLAGS) $(RL_CPPFLAGS) $(CFLAGS) -Wno-unused-parameter -c -o $@ $<

$(CLI):  $(CLI_OBJS)
	$(CC) $(CFLAGS) $(CLI_OBJS) $(LDFLAGS) -o $@ $(CLI_LIBDEPS) $(RL_LIBDEPS)

cli_handlers.o:	cli_handlers.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-unused-parameter -c -o $@ $<

install:
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -m 755 $(EXEC) $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -m 755 $(CLI) $(DESTDIR)$(bindir)
ifdef SYSTEMD
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(unitdir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).service $(DESTDIR)$(unitdir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).socket $(DESTDIR)$(unitdir)
endif
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(mandir)/man8
	$(INSTALL_PROGRAM) -m 644 $(EXEC).8 $(DESTDIR)$(mandir)/man8
	$(INSTALL_PROGRAM) -m 644 $(CLI).8 $(DESTDIR)$(mandir)/man8

uninstall:
	$(RM) $(DESTDIR)$(bindir)/$(EXEC) $(DESTDIR)$(bindir)/$(CLI)
	$(RM) $(DESTDIR)$(mandir)/man8/$(EXEC).8
	$(RM) $(DESTDIR)$(mandir)/man8/$(CLI).8
	$(RM) $(DESTDIR)$(unitdir)/$(EXEC).service
	$(RM) $(DESTDIR)$(unitdir)/$(EXEC).socket

clean: dep_clean
	$(RM) core *.o $(EXEC) $(CLI)

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(RM) $(OBJS:.o=.d)
