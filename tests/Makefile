include ../Makefile.inc

CFLAGS += $(BIN_CFLAGS) -I$(multipathdir) -I$(mpathcmddir)
LIBDEPS += -L$(multipathdir) -lmultipath -lcmocka

TESTS := uevent parser util dmevents hwtable blacklist unaligned vpd pgpolicy

.SILENT: $(TESTS:%=%.o)
.PRECIOUS: $(TESTS:%=%-test)

all:	$(TESTS:%=%.out)

# test-specific linker flags
# XYZ-test-TESTDEPS: test libraries containing __wrap_xyz functions
# XYZ-test_OBJDEPS: object files from libraries to link in explicitly
#    That may be necessary if functions called from the object file are wrapped
#    (wrapping works only for symbols which are undefined after processing a
#    linker input file).
# XYZ-test_LIBDEPS: Additional libs to link for this test

dmevents-test_LIBDEPS = -lpthread -ldevmapper -lurcu
hwtable-test_TESTDEPS := test-lib.o
hwtable-test_OBJDEPS := ../libmultipath/discovery.o ../libmultipath/blacklist.o \
	../libmultipath/prio.o ../libmultipath/callout.o ../libmultipath/structs.o
hwtable-test_LIBDEPS := -ludev -lpthread -ldl
blacklist-test_OBJDEPS := ../libmultipath/blacklist.o
blacklist-test_LIBDEPS := -ludev
vpd-test_OBJDEPS :=  ../libmultipath/discovery.o
vpd-test_LIBDEPS := -ludev -lpthread -ldl

lib/libchecktur.so:
	mkdir lib
	ln -t lib ../libmultipath/{checkers,prioritizers,foreign}/*.so

%.out:	%-test lib/libchecktur.so
	@echo == running $< ==
	@LD_LIBRARY_PATH=$(multipathdir):$(mpathcmddir) ./$< >$@

OBJS = $(TESTS:%=%.o) test-lib.o

clean: dep_clean
	$(RM) $(TESTS:%=%-test) $(TESTS:%=%.out) $(OBJS)
	$(RM) -rf lib

.SECONDARY: $(OBJS)

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(RM) $(OBJS:.o=.d)

%.o.wrap:	%.c
	@sed -n 's/^.*__wrap_\([a-zA-Z0-9_]*\).*$$/-Wl,--wrap=\1/p' $< | \
		sort -u | tr '\n' ' ' >$@

# COLON will get expanded during second expansion below
COLON:=:
.SECONDEXPANSION:
%-test:	%.o %.o.wrap $$($$@_OBJDEPS) $$($$@_TESTDEPS) $$($$@_TESTDEPS$$(COLON).o=.o.wrap) \
		$(multipathdir)/libmultipath.so Makefile
	$(CC) $(CFLAGS) -o $@ $(LDFLAGS) $< $($@_TESTDEPS) $($@_OBJDEPS) \
		$(LIBDEPS) $($@_LIBDEPS) \
		$(shell cat $<.wrap) $(foreach dep,$($@_TESTDEPS),$(shell cat $(dep).wrap))
