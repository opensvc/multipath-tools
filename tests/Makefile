include ../Makefile.inc

# directory where to run the tests
TESTDIR := $(CURDIR)

# Test special behavior of gcc 4.8 with nested initializers
# gcc 4.8 compiles blacklist.c only with -Wno-missing-field-initializers
TEST_MISSING_INITIALIZERS = $(shell \
	echo 'struct A {int a, b;}; struct B {struct A a; int b;} b = {.a.a=1};' | \
		$(CC) -c -Werror -Wmissing-field-initializers -o /dev/null -xc - >/dev/null 2>&1 \
	|| echo -Wno-missing-field-initializers)
W_MISSING_INITIALIZERS := $(call TEST_MISSING_INITIALIZERS)

CPPFLAGS += -I$(multipathdir) -I$(mpathutildir) -I$(mpathcmddir) -DTESTCONFDIR=\"$(TESTDIR)/conf.d\"
CFLAGS += $(BIN_CFLAGS) -Wno-unused-parameter $(W_MISSING_INITIALIZERS)
LIBDEPS += -L. -L $(mpathutildir) -L$(mpathcmddir) -lmultipath -lmpathutil -lmpathcmd -lcmocka

TESTS := uevent parser util dmevents hwtable blacklist unaligned vpd pgpolicy \
	 alias directio valid devt mpathvalid strbuf sysfs
HELPERS := test-lib.o test-log.o

.SILENT: $(TESTS:%=%.o)
.PRECIOUS: $(TESTS:%=%-test)

all:	$(TESTS:%=%.out)
progs:	$(TESTS:%=%-test) lib/libchecktur.so
valgrind:	$(TESTS:%=%.vgr)

# test-specific compiler flags
# XYZ-test_FLAGS: Additional compiler flags for this test

ifneq ($(wildcard directio_test_dev),)
DIO_TEST_DEV = $(shell sed -n -e 's/^[[:space:]]*DIO_TEST_DEV[[:space:]]*=[[:space:]]*\([^[:space:]\#]\+\).*/\1/p' < directio_test_dev)
endif
ifneq ($(DIO_TEST_DEV),)
directio-test_FLAGS := -DDIO_TEST_DEV=\"$(DIO_TEST_DEV)\"
endif
mpathvalid-test_FLAGS := -I$(mpathvaliddir)

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
	$(multipathdir)/structs.o $(multipathdir)/propsel.o
hwtable-test_LIBDEPS := -ludev -lpthread -ldl
blacklist-test_TESTDEPS := test-log.o
blacklist-test_OBJDEPS := $(multipathdir)/blacklist.o
blacklist-test_LIBDEPS := -ludev
vpd-test_OBJDEPS :=  $(multipathdir)/discovery.o
vpd-test_LIBDEPS := -ludev -lpthread -ldl
alias-test_TESTDEPS := test-log.o
alias-test_LIBDEPS := -lpthread -ldl
valid-test_OBJDEPS := $(multipathdir)/valid.o $(multipathdir)/discovery.o
valid-test_LIBDEPS := -ludev -lpthread -ldl
devt-test_LIBDEPS := -ludev
mpathvalid-test_LIBDEPS := -ludev -lpthread -ldl
mpathvalid-test_OBJDEPS := $(mpathvaliddir)/mpath_valid.o
ifneq ($(DIO_TEST_DEV),)
directio-test_LIBDEPS := -laio
endif
strbuf-test_OBJDEPS := $(mpathutildir)/strbuf.o
sysfs-test_TESTDEPS := test-log.o
sysfs-test_OBJDEPS := $(multipathdir)/sysfs.o $(mpathutildir)/util.o
sysfs-test_LIBDEPS := -ludev -lpthread -ldl

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $($*-test_FLAGS) -c -o $@ $<

lib/libchecktur.so:
	mkdir -p lib
	cd lib && ln -s ../$(multipathdir)/*/*.so .

%.out:	%-test lib/libchecktur.so
	@echo == running $< ==
	@LD_LIBRARY_PATH=.:$(mpathutildir):$(mpathcmddir) ./$< >$@ 2>&1

%.vgr:  %-test lib/libchecktur.so
	@echo == running valgrind for $< ==
	@LD_LIBRARY_PATH=.:$(mpathutildir):$(mpathcmddir) \
		valgrind --leak-check=full --error-exitcode=128 ./$< >$@ 2>&1

OBJS = $(TESTS:%=%.o) $(HELPERS)

test_clean:
	$(RM) $(TESTS:%=%.out) $(TESTS:%=%.vgr) *.so*

valgrind_clean:
	$(RM) $(TESTS:%=%.vgr)

clean: test_clean valgrind_clean dep_clean
	$(RM) $(TESTS:%=%-test) $(OBJS) *.o.wrap
	$(RM) -rf lib conf.d

.SECONDARY: $(OBJS)

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(RM) $(OBJS:.o=.d)

%.o.wrap:	%.c
	@sed -n 's/^.*__wrap_\([a-zA-Z0-9_]*\).*$$/-Wl,--wrap=\1/p' $< | \
		sort -u | tr '\n' ' ' >$@

libmultipath.so.0: $(multipathdir)/libmultipath.so.0
	make -C $(multipathdir) configdir=$(TESTDIR)/conf.d plugindir=$(TESTDIR)/lib test-lib

# COLON will get expanded during second expansion below
COLON:=:
.SECONDEXPANSION:
%-test:	%.o %.o.wrap $$($$@_OBJDEPS) $$($$@_TESTDEPS) $$($$@_TESTDEPS$$(COLON).o=.o.wrap) \
		libmultipath.so.0 $(mpathutildir)/libmpathutil.so.0 $(mpathcmddir)/libmpathcmd.so.0 Makefile
	$(CC) $(CFLAGS) -o $@ $(LDFLAGS) $< $($@_TESTDEPS) $($@_OBJDEPS) \
		$(LIBDEPS) $($@_LIBDEPS) \
		$(shell cat $<.wrap) $(foreach dep,$($@_TESTDEPS),$(shell cat $(dep).wrap))
