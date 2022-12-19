#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#
TOPDIR=../..
include ../../Makefile.inc

CPPFLAGS += -I$(multipathdir) -I$(mpathutildir) -I$(nvmedir)
CFLAGS += $(LIB_CFLAGS)
LDFLAGS += -L$(multipathdir) -L$(mpathutildir)
LIBDEPS = -lmultipath -lmpathutil -ludev -lpthread -lrt

LIBS = libforeign-nvme.so

all: $(LIBS)

libforeign-%.so: %.o
	$(Q)$(CC) $(LDFLAGS) $(SHARED_FLAGS) -o $@ $^ $(LIBDEPS)

install:
	$(Q)$(INSTALL_PROGRAM) -m 755 $(LIBS) $(DESTDIR)$(plugindir)

uninstall:
	$(Q)for file in $(LIBS); do $(RM) $(DESTDIR)$(plugindir)/$$file; done

clean: dep_clean
	$(Q)$(RM) core *.a *.o *.gz *.so

OBJS := $(LIBS:libforeign-%.so=%.o)
.SECONDARY: $(OBJS)

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(Q)$(RM) $(OBJS:.o=.d)
