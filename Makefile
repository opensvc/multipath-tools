#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#

LIB_BUILDDIRS := \
	libmpathcmd \
	libmpathutil \
	libmultipath \
	libmpathpersist \
	libmpathvalid

ifneq ($(ENABLE_LIBDMMP),0)
LIB_BUILDDIRS += \
	libdmmp
endif

BUILDDIRS := $(LIB_BUILDDIRS) \
	libmultipath/prioritizers \
	libmultipath/checkers \
	libmultipath/foreign \
	multipath \
	multipathd \
	mpathpersist \
	kpartx


BUILDDIRS.clean := $(BUILDDIRS:=.clean) tests.clean

.PHONY:	$(BUILDDIRS)

all:	$(BUILDDIRS)

$(BUILDDIRS):
	$(MAKE) -C $@

$(LIB_BUILDDIRS:=.abi): $(LIB_BUILDDIRS)
	$(MAKE) -C ${@:.abi=} abi

# Create formal representation of the ABI
# Useful for verifying ABI compatibility
# Requires abidw from the abigail suite (https://sourceware.org/libabigail/)
.PHONY: abi
abi:	$(LIB_BUILDDIRS:=.abi)
	mkdir -p $@
	ln -ft $@ $(LIB_BUILDDIRS:=/*.abi)

abi.tar.gz:	abi
	tar cfz $@ abi

# Check the ABI against a reference.
# This requires the ABI from a previous run to be present
# in the directory "reference-abi"
# Requires abidiff from the abigail suite
abi-test:	abi reference-abi $(wildcard abi/*.abi)
	@err=0; \
	for lib in abi/*.abi; do \
	    diff=$$(abidiff "reference-$$lib" "$$lib") || { \
	        err=1; \
		echo "==== ABI differences in for $$lib ===="; \
		echo "$$diff"; \
	    }; \
	done >$@; \
	if [ $$err -eq 0 ]; then \
	    echo "*** OK, ABI unchanged ***"; \
	else \
	    echo "*** WARNING: ABI has changed, see file $@ ***"; \
	fi; \
	[ $$err -eq 0 ]

# Create compile_commands.json, useful for using clangd with an IDE
# Requires bear (https://github.com/rizsotto/Bear)
compile_commands.json: Makefile Makefile.inc $(BUILDDIRS:=/Makefile)
	$(MAKE) clean
	bear -- $(MAKE)

libmpathutil libdmmp: libmpathcmd
libmultipath: libmpathutil
libmpathpersist libmpathvalid multipath multipathd: libmultipath
libmultipath/prioritizers libmultipath/checkers libmultipath/foreign: libmultipath
mpathpersist multipathd:  libmpathpersist

libmultipath/checkers.install \
	libmultipath/prioritizers.install \
	libmultipath/foreign.install: libmultipath.install

$(BUILDDIRS.clean):
	$(MAKE) -C ${@:.clean=} clean

$(BUILDDIRS:=.install): $(BUILDDIRS)
	$(MAKE) -C ${@:.install=} install

$(BUILDDIRS:=.uninstall):
	$(MAKE) -C ${@:.uninstall=} uninstall

clean: $(BUILDDIRS.clean)
	rm -rf abi abi.tar.gz abi-test compile_commands.json

install: $(BUILDDIRS:=.install)
uninstall: $(BUILDDIRS:=.uninstall)

test-progs:	all
	$(MAKE) -C tests progs

test:	test-progs
	$(MAKE) -C tests all

valgrind-test:	all
	$(MAKE) -C tests valgrind

.PHONY:	TAGS
TAGS:
	etags -a libmultipath/*.c
	etags -a libmultipath/*.h
	etags -a multipathd/*.c
	etags -a multipathd/*.h
