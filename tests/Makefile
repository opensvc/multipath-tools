include ../Makefile.inc

# directory where to run the tests
TESTDIR := $(CURDIR)

CPPFLAGS += -I$(multipathdir) -I$(mpathutildir) -I$(mpathcmddir) -I$(daemondir) \
	-DTESTCONFDIR=\"$(TESTDIR)/conf.d\"
CFLAGS += $(BIN_CFLAGS) -Wno-unused-parameter -Wno-unused-function $(W_MISSING_INITIALIZERS)
LIBDEPS += -L. -L $(mpathutildir) -L$(mpathcmddir) -lmultipath -lmpathutil -lmpathcmd -lcmocka

TESTS := uevent parser util dmevents hwtable blacklist unaligned vpd pgpolicy \
	 alias directio valid devt mpathvalid strbuf sysfs features cli mapinfo
HELPERS := test-lib.o test-log.o

.PRECIOUS: $(TESTS:%=%-test)

all:	$(TESTS:%=%.out)
progs:	$(TESTS:%=%-test) lib/libchecktur.so
valgrind:	$(TESTS:%=%.vgr)

# test-specific compiler flags
# XYZ-test_FLAGS: Additional compiler flags for this test

mpathvalid-test_FLAGS := -I$(mpathvaliddir)
features-test_FLAGS := -I$(multipathdir)/nvme

# test-specific linker flags
# XYZ-test_TESTDEPS: test libraries containing __wrap_xyz functions
# XYZ-test_OBJDEPS: object files from libraries to link in explicitly
#    That may be necessary if functions called from the object file are wrapped
#    (wrapping works only for symbols which are undefined after processing a
#    linker input file).
#    Some object files, e.g. "config.o", are compiled separately for the
#    unit tests. Look for OBJS-U in libmultipath/Makefile. Make sure to use the
#    unit test file, e.g. "config-test.o", in XYZ-test_OBJDEPS
# XYZ-test_LIBDEPS: Additional libs to link for this test

dmevents-test_OBJDEPS = $(multipathdir)/devmapper.o
dmevents-test_LIBDEPS = -lpthread -ldevmapper -lurcu
hwtable-test_TESTDEPS := test-lib.o
hwtable-test_OBJDEPS := $(multipathdir)/discovery.o $(multipathdir)/blacklist.o \
	$(multipathdir)/structs_vec.o $(multipathdir)/structs.o $(multipathdir)/propsel.o \
	$(mpathutildir)/mt-libudev.o
hwtable-test_LIBDEPS := -ludev -lpthread -ldl
blacklist-test_TESTDEPS := test-log.o
blacklist-test_OBJDEPS := $(mpathutildir)/mt-libudev.o
blacklist-test_LIBDEPS := -ludev -lpthread -ldl
vpd-test_OBJDEPS :=  $(multipathdir)/discovery.o $(mpathutildir)/mt-libudev.o
vpd-test_LIBDEPS := -ludev -lpthread -ldl
alias-test_TESTDEPS := test-log.o
alias-test_OBJDEPS := $(mpathutildir)/util.o $(mpathutildir)/mt-libudev.o
alias-test_LIBDEPS := -ludev -lpthread -ldl
valid-test_OBJDEPS := $(multipathdir)/valid.o $(multipathdir)/discovery.o $(mpathutildir)/mt-libudev.o
valid-test_LIBDEPS := -lmount -ludev -lpthread -ldl
devt-test_LIBDEPS := -ludev -lpthread -ldl
devt-test_OBJDEPS := $(mpathutildir)/mt-libudev.o
mpathvalid-test_LIBDEPS := -ludev -lpthread -ldl
mpathvalid-test_OBJDEPS := $(mpathvaliddir)/mpath_valid.o $(mpathutildir)/mt-libudev.o
directio-test_LIBDEPS := -laio
strbuf-test_OBJDEPS := $(mpathutildir)/strbuf.o
sysfs-test_TESTDEPS := test-log.o
sysfs-test_OBJDEPS := $(multipathdir)/sysfs.o $(mpathutildir)/util.o $(mpathutildir)/mt-libudev.o
sysfs-test_LIBDEPS := -ludev -lpthread -ldl
features-test_LIBDEPS := -ludev -lpthread
features-test_OBJDEPS := $(mpathutildir)/mt-libudev.o
cli-test_OBJDEPS := $(daemondir)/cli.o
mapinfo-test_LIBDEPS = -lpthread -ldevmapper

%.o: %.c
	@echo building $@ because of $?
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) $($*-test_FLAGS) -c -o $@ $<

lib/libchecktur.so:
	@mkdir -p lib
	$(Q)cd lib && ln -s ../$(multipathdir)/*/*.so .

%.out:	%-test lib/libchecktur.so
	@echo == running $< ==
	@LD_LIBRARY_PATH=.:$(mpathutildir):$(mpathcmddir) ./$< >$@ 2>&1 || { cat "$@"; false; }

%.vgr:  %-test lib/libchecktur.so
	@echo == running valgrind for $< ==
	@LD_LIBRARY_PATH=.:$(mpathutildir):$(mpathcmddir) \
		valgrind --leak-check=full --error-exitcode=128 ./$< >$@ 2>&1

OBJS = $(TESTS:%=%.o) $(HELPERS)

test_clean:
	$(Q)$(RM) $(TESTS:%=%.out) $(TESTS:%=%.vgr) *.so*

valgrind_clean:
	$(Q)$(RM) $(TESTS:%=%.vgr)

clean: test_clean valgrind_clean dep_clean
	$(Q)$(RM) $(TESTS:%=%-test) $(OBJS) *.o.wrap
	$(Q)$(RM) -rf lib conf.d

.SECONDARY: $(OBJS) $(foreach T,$(TESTS),$($T-test_OBJDEPS)) $(HELPERS:%=%.wrap)

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(Q)$(RM) $(OBJS:.o=.d)

# Parse the C code for __wrap_xyz() functions and generate linker options from them.
# See comment in wrap64.h
%.o.wrap:	%.c
	$(Q)$(CC) $(OPTFLAGS) $(CPPFLAGS) $($*-test_FLAGS) -E $< | \
		sed -n 's/^.*__wrap_\([a-zA-Z0-9_]*\).*$$/-Wl,--wrap=\1/p' | \
		sort -u | tr '\n' ' ' >$@

# Pass the original values of CFLAGS etc. to the sub-make, which will include
# Makefile.in again. Otherwise, the flags would be added twice.
libmultipath.so.0: $(multipathdir)/libmultipath.so.0
	@CFLAGS="$(ORIG_CFLAGS)" CPPFLAGS="$(ORIG_CPPFLAGS)" LDFLAGS="$(ORIG_LDFLAGS)" \
	$(MAKE) -C $(multipathdir) configdir=$(TESTDIR)/conf.d plugindir=$(TESTDIR)/lib test-lib

# COLON will get expanded during second expansion below
COLON:=:
.SECONDEXPANSION:
%-test:	%.o %.o.wrap $$($$@_OBJDEPS) $$($$@_TESTDEPS) $$($$@_TESTDEPS$$(COLON).o=.o.wrap) \
		libmultipath.so.0 $(mpathutildir)/libmpathutil.so.0 $(mpathcmddir)/libmpathcmd.so.0 Makefile
	@echo building $@
	$(Q)$(CC) $(CFLAGS) -o $@ $(LDFLAGS) $< $($@_TESTDEPS) $($@_OBJDEPS) \
		$(LIBDEPS) $($@_LIBDEPS) \
		$(shell cat $<.wrap) $(foreach dep,$($@_TESTDEPS),$(shell cat $(dep).wrap))
