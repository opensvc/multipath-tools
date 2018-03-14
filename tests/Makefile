include ../Makefile.inc

CFLAGS += $(BIN_CFLAGS) -I$(multipathdir) -I$(mpathcmddir)
LIBDEPS += -L$(multipathdir) -lmultipath -lcmocka

TESTS := uevent parser util dmevents

.SILENT: $(TESTS:%=%.o)
.PRECIOUS: $(TESTS:%=%-test)

all:	$(TESTS:%=%.out)

dmevents-test: dmevents.o ../multipathd/dmevents.c globals.c $(multipathdir)/libmultipath.so
	@$(CC) -o $@ $< $(LDFLAGS) $(LIBDEPS) -lpthread -ldevmapper -lurcu -Wl,--wrap=open -Wl,--wrap=close -Wl,--wrap=dm_is_mpath -Wl,--wrap=dm_geteventnr -Wl,--wrap=ioctl -Wl,--wrap=libmp_dm_task_create -Wl,--wrap=dm_task_no_open_count -Wl,--wrap=dm_task_run -Wl,--wrap=dm_task_get_names -Wl,--wrap=dm_task_destroy -Wl,--wrap=poll -Wl,--wrap=remove_map_by_alias -Wl,--wrap=update_multipath

%-test:	%.o globals.c $(multipathdir)/libmultipath.so
	@$(CC) -o $@ $< $(LDFLAGS) $(LIBDEPS)

%.out:	%-test
	@echo == running $< ==
	@LD_LIBRARY_PATH=$(multipathdir):$(mpathcmddir) ./$< >$@

clean: dep_clean
	rm -f $(TESTS:%=%-test) $(TESTS:%=%.out) $(TESTS:%=%.o)

OBJS = $(TESTS:%=%.o)
.SECONDARY: $(OBJS)

include $(wildcard $(OBJS:.o=.d))

dep_clean:
	$(RM) $(OBJS:.o=.d)
