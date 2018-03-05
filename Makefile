#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#

BUILDDIRS = \
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

all: recurse

recurse:
	@for dir in $(BUILDDIRS); do $(MAKE) -C $$dir || exit $?; done

recurse_clean:
	@for dir in $(BUILDDIRS); do \
	$(MAKE) -C $$dir clean || exit $?; \
	done
	$(MAKE) -C tests clean

recurse_install:
	@for dir in $(BUILDDIRS); do \
	$(MAKE) -C $$dir install || exit $?; \
	done

recurse_uninstall:
	@for dir in $(BUILDDIRS); do \
	$(MAKE) -C $$dir uninstall || exit $?; \
	done

clean: recurse_clean

install: recurse_install

uninstall: recurse_uninstall

test:	all
	$(MAKE) -C tests

.PHONY:	TAGS
TAGS:
	etags -a libmultipath/*.c
	etags -a libmultipath/*.h
	etags -a multipathd/*.c
	etags -a multipathd/*.h
