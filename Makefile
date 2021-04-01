#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#

BUILDDIRS := \
	libmpathcmd \
	libmultipath \
	libmultipath/prioritizers \
	libmultipath/checkers \
	libmultipath/foreign \
	libmpathpersist \
	libmpathvalid \
	multipath \
	multipathd \
	mpathpersist \
	kpartx

ifneq ($(ENABLE_LIBDMMP),0)
BUILDDIRS += \
	libdmmp
endif

BUILDDIRS.clean := $(BUILDDIRS:=.clean) tests.clean

.PHONY:	$(BUILDDIRS) $(BUILDDIRS:=.uninstall) $(BUILDDIRS:=.install) $(BUILDDIRS.clean)

all:	$(BUILDDIRS)

$(BUILDDIRS):
	$(MAKE) -C $@

libmultipath libdmmp: libmpathcmd
libmpathpersist libmpathvalid multipath multipathd: libmultipath
libmultipath/prioritizers libmultipath/checkers libmultipath/foreign: libmultipath
mpathpersist multipathd:  libmpathpersist

libmultipath/checkers.install \
	libmultipath/prioritizers.install \
	libmultipath/foreign.install: libmultipath.install

$(BUILDDIRS.clean):
	$(MAKE) -C ${@:.clean=} clean

$(BUILDDIRS:=.install):
	$(MAKE) -C ${@:.install=} install

$(BUILDDIRS:=.uninstall):
	$(MAKE) -C ${@:.uninstall=} uninstall

clean: $(BUILDDIRS.clean)
install: all $(BUILDDIRS:=.install)
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
