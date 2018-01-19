# Makefile
#
# Copyright (C) 2015-2016 Gris Ge <fge@redhat.com>
#
include ../../Makefile.inc

_libdmmpdir=../$(libdmmpdir)
_mpathcmddir=../$(mpathcmddir)

TEST_EXEC = libdmmp_test
SPD_TEST_EXEC = libdmmp_speed_test
CFLAGS += -I$(_libdmmpdir)
LDFLAGS += -L$(_libdmmpdir) -ldmmp

all: $(TEST_EXEC) $(SPD_TEST_EXEC)

check: $(TEST_EXEC) $(SPD_TEST_EXEC)
	sudo env LD_LIBRARY_PATH=$(_libdmmpdir):$(_mpathcmddir) \
		valgrind --quiet --leak-check=full \
		--show-reachable=no --show-possibly-lost=no \
		--trace-children=yes --error-exitcode=1 \
		./$(TEST_EXEC)
	$(MAKE) speed_test

speed_test: $(SPD_TEST_EXEC)
	sudo env LD_LIBRARY_PATH=$(_libdmmpdir):$(_mpathcmddir) \
		time -p ./$(SPD_TEST_EXEC)

clean: dep_clean
	rm -f $(TEST_EXEC) $(SPD_TEST_EXEC)

OBJS = $(TEST_EXEC).o $(SPD_TEST_EXEC).o
include $(wildcard $(OBJS:.o=.d))


dep_clean:
	$(RM) $(OBJS:.o=.d)
