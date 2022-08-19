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
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -o $@ $^ $(LIBDEPS)

install:
	$(INSTALL_PROGRAM) -m 755 $(LIBS) $(DESTDIR)$(libdir)

uninstall:
	for file in $(LIBS); do $(RM) $(DESTDIR)$(libdir)/$$file; done

clean: dep_clean
	$(RM) core *.a *.o *.gz *.so

OBJS := $(LIBS:libforeign-%.so=%.o)
.SECONDARY: $(OBJS)

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(RM) $(OBJS:.o=.d)
