# Makefile
#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@free.fr>

BUILD = glibc

#
# Try to supply the linux kernel headers.
#
ifeq    ($(KRNLSRC),)
KRNLLIB = /lib/modules/$(shell uname -r)
ifeq    ($(shell test -r $(KRNLLIB)/source && echo 1),1)
KRNLSRC = $(KRNLLIB)/source
KRNLOBJ = $(KRNLLIB)/build
else
KRNLSRC = $(KRNLLIB)/build
KRNLOBJ = $(KRNLLIB)/build
endif
endif
export KRNLSRC
export KRNLOBJ

BUILDDIRS = $(shell find . -name Makefile -mindepth 2 -exec dirname {} \;)

VERSION = $(shell basename ${PWD} | cut -d'-' -f3)

all: recurse

recurse:
	@for dir in $(BUILDDIRS); do \
	$(MAKE) -C $$dir BUILD=$(BUILD) VERSION=$(VERSION) \
		KRNLSRC=$(KRNLSRC) KRNLOBJ=$(KRNLOBJ) || exit $?; \
	done

recurse_clean:
	@for dir in $(BUILDDIRS); do\
	$(MAKE) -C $$dir clean || exit $?; \
	done

recurse_install:
	@for dir in $(BUILDDIRS); do\
	$(MAKE) -C $$dir install || exit $?; \
	done

recurse_uninstall:
	@for dir in $(BUILDDIRS); do\
	$(MAKE) -C $$dir uninstall || exit $?; \
	done

clean:	recurse_clean
	rm -f multipath-tools.spec
	rm -rf rpms

install:	recurse_install

uninstall:	recurse_uninstall

release:
	sed -e "s/__VERSION__/${VERSION}/" \
	multipath-tools.spec.in > multipath-tools.spec

rpm: release
	rpmbuild -bb multipath-tools.spec
