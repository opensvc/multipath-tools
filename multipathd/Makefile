include ../Makefile.inc

ifneq ($(call check_func,dm_task_get_errno,$(DEVMAPPER_INCDIR)/libdevmapper.h),0)
	CPPFLAGS += -DLIBDM_API_GET_ERRNO
endif

ifneq ($(call check_var,ELS_DTAG_LNK_INTEGRITY,$(LINUX_HEADERS_INCDIR)/scsi/fc/fc_els.h),0)
	CPPFLAGS += -DFPIN_EVENT_HANDLER
	FPIN_SUPPORT = 1
endif
#
# debugging stuff
#
#CPPFLAGS += -DLCKDBG
#CPPFLAGS += -D_DEBUG_
#CPPFLAGS += -DLOGDBG

CPPFLAGS += -I$(multipathdir) -I$(mpathpersistdir) -I$(mpathcmddir) -I$(thirdpartydir) \
	$(shell $(PKGCONFIG) --modversion liburcu 2>/dev/null | \
		awk -F. '{ printf("-DURCU_VERSION=0x%06x", 256 * ( 256 * $$1 + $$2) + $$3); }')
CFLAGS += $(BIN_CFLAGS)
LDFLAGS += $(BIN_LDFLAGS)
LIBDEPS += -L$(multipathdir) -lmultipath -L$(mpathpersistdir) -lmpathpersist \
	   -L$(mpathcmddir) -lmpathcmd -ludev -ldl -lurcu -lpthread \
	   -ldevmapper

ifeq ($(READLINE),libedit)
CPPFLAGS += -DUSE_LIBEDIT
LIBDEPS += -ledit
endif
ifeq ($(READLINE),libreadline)
CPPFLAGS += -DUSE_LIBREADLINE
LIBDEPS += -lreadline
endif

ifdef SYSTEMD
	CPPFLAGS += -DUSE_SYSTEMD=$(SYSTEMD)
	ifeq ($(shell test $(SYSTEMD) -gt 209 && echo 1), 1)
		LIBDEPS += -lsystemd
	else
		LIBDEPS += -lsystemd-daemon
	endif
endif
ifeq ($(ENABLE_DMEVENTS_POLL),0)
	CPPFLAGS += -DNO_DMEVENTS_POLL
endif

OBJS = main.o pidfile.o uxlsnr.o uxclnt.o cli.o cli_handlers.o waiter.o \
       dmevents.o init_unwinder.o

ifeq ($(FPIN_SUPPORT),1)
OBJS += fpin_handlers.o
endif



EXEC = multipathd

all : $(EXEC)

$(EXEC): $(OBJS) $(multipathdir)/libmultipath.so $(mpathcmddir)/libmpathcmd.so
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $(EXEC) $(LIBDEPS)

cli_handlers.o:	cli_handlers.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-unused-parameter -c -o $@ $<

install:
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(bindir)
	$(INSTALL_PROGRAM) -m 755 $(EXEC) $(DESTDIR)$(bindir)
ifdef SYSTEMD
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(unitdir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).service $(DESTDIR)$(unitdir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).socket $(DESTDIR)$(unitdir)
endif
	$(INSTALL_PROGRAM) -d $(DESTDIR)$(man8dir)
	$(INSTALL_PROGRAM) -m 644 $(EXEC).8 $(DESTDIR)$(man8dir)

uninstall:
	$(RM) $(DESTDIR)$(bindir)/$(EXEC)
	$(RM) $(DESTDIR)$(man8dir)/$(EXEC).8
	$(RM) $(DESTDIR)$(unitdir)/$(EXEC).service
	$(RM) $(DESTDIR)$(unitdir)/$(EXEC).socket

clean: dep_clean
	$(RM) core *.o $(EXEC)

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(RM) $(OBJS:.o=.d)
