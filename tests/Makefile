include ../Makefile.inc

CFLAGS += $(BIN_CFLAGS) -I$(multipathdir) -I$(mpathcmddir)
LIBDEPS += -L$(multipathdir) -lmultipath -lcmocka

TESTS := uevent

.SILENT: $(TESTS:%=%.o)
.PRECIOUS: $(TESTS:%=%-test)

%-test:	%.o globals.c $(multipathdir)/libmultipath.so
	@$(CC) -o $@ $< $(LDFLAGS) $(LIBDEPS)

%.out:	%-test
	@echo == running $< ==
	@LD_LIBRARY_PATH=$(multipathdir):$(mpathcmddir) ./$< >$@

all:	$(TESTS:%=%.out)

clean: dep_clean
	rm -f $(TESTS:%=%-test) $(TESTS:%=%.out) $(TESTS:%=%.o)

OBJS = $(TESTS:%=%.o)
.SECONDARY: $(OBJS)

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(RM) $(OBJS:.o=.d)
