# Makefile
#
# Copyright (C) 2003 Christophe Varoqui, <christophe.varoqui@free.fr>
#
DEBUG = 0

SUBDIRS	= $(shell find . -type d -mindepth 1 -maxdepth 1|cut -c3-)

all:
	@for DIR in $(SUBDIRS); do \
		$(MAKE) -C $$DIR BUILD=$(BUILD) VERSION=$(VERSION); \
	done

install:
	@for DIR in $(SUBDIRS); do \
		$(MAKE) -C $$DIR install; \
	done

uninstall:
	@for DIR in $(SUBDIRS); do \
		$(MAKE) -C $$DIR uninstall; \
	done

clean:
	@for DIR in $(SUBDIRS); do \
		$(MAKE) -C $$DIR clean; \
	done
