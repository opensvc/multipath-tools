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
libmpathpersist multipath multipathd: libmultipath
mpathpersist multipathd:  libmpathpersist

$(BUILDDIRS.clean):
	$(MAKE) -C ${@:.clean=} clean

$(BUILDDIRS:=.install):
	$(MAKE) -C ${@:.install=} install

$(BUILDDIRS:=.uninstall):
	$(MAKE) -C ${@:.uninstall=} uninstall

clean: $(BUILDDIRS.clean)
install: $(BUILDDIRS:=.install)
uninstall: $(BUILDDIRS:=.uninstall)

test:	all
	$(MAKE) -C tests

.PHONY:	TAGS
TAGS:
	etags -a libmultipath/*.c
	etags -a libmultipath/*.h
	etags -a multipathd/*.c
	etags -a multipathd/*.h
