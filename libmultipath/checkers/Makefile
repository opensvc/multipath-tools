#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#
include ../../Makefile.inc

CFLAGS += $(LIB_CFLAGS) -I..
LDFLAGS += -L.. -lmultipath
LIBDEPS = -lmultipath -laio -lpthread -lrt

# If you add or remove a checker also update multipath/multipath.conf.5
LIBS= \
	libcheckcciss_tur.so \
	libcheckreadsector0.so \
	libchecktur.so \
	libcheckdirectio.so \
	libcheckemc_clariion.so \
	libcheckhp_sw.so \
	libcheckrdac.so

all: $(LIBS)

libcheck%.so: %.o
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -o $@ $^ $(LIBDEPS)

install:
	$(INSTALL_PROGRAM) -m 755 $(LIBS) $(DESTDIR)$(libdir)

uninstall:
	for file in $(LIBS); do $(RM) $(DESTDIR)$(libdir)/$$file; done

clean: dep_clean
	$(RM) core *.a *.o *.gz *.so

OBJS := $(LIBS:libcheck%.so=%.o)
.SECONDARY: $(OBJS)

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(RM) $(OBJS:.o=.d)
