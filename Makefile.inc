# -*- Makefile -*-
# Copyright (C) 2004 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#

#
# Uncomment to disable libdmmp support
# ENABLE_LIBDMMP = 0
#
# Uncomment to disable dmevents polling support
# ENABLE_DMEVENTS_POLL = 0
#
# Readline library to use, libedit, libreadline, or empty
# Caution: Using libreadline may make the multipathd binary undistributable,
# see https://github.com/opensvc/multipath-tools/issues/36
READLINE :=

# List of scsi device handler modules to load on boot, e.g.
# SCSI_DH_MODULES_PRELOAD := scsi_dh_alua scsi_dh_rdac
SCSI_DH_MODULES_PRELOAD :=

EXTRAVERSION := $(shell rev=$$(git rev-parse --short=7 HEAD 2>/dev/null); echo $${rev:+-g$$rev})

# PKGCONFIG must be read from the environment to enable compilation
# in Debian multiarch setups
PKGCONFIG	?= pkg-config

ifeq ($(TOPDIR),)
	TOPDIR	= ..
endif
ifneq ($(TOPDIR),.)
include $(TOPDIR)/config.mk
endif

# Paths. All these can be overridden on the "make" command line.
prefix		:=
# Prefix for binaries
exec_prefix	:= $(prefix)
# Prefix for non-essential libraries (libdmmp)
usr_prefix	:= $(prefix)
# Where to install systemd-related files. systemd is usually installed under /usr
# Note: some systemd installations use separate "prefix" and "rootprefix".
# In this case, override only unitdir to use systemd's "rootprefix" instead of $(systemd_prefix)
systemd_prefix  := /usr
unitdir		:= $(systemd_prefix)/lib/systemd/system
tmpfilesdir	:= $(systemd_prefix)/lib/tmpfiles.d
modulesloaddir	:= $(systemd_prefix)/lib/modules-load.d
libudevdir	:= $(systemd_prefix)/lib/udev
udevrulesdir	:= $(libudevdir)/rules.d
bindir		:= $(exec_prefix)/sbin
mandir		:= $(usr_prefix)/share/man
LIB		:= $(if $(shell test -d /lib64 && echo 1),lib64,lib)
syslibdir	:= $(prefix)/$(LIB)
usrlibdir	:= $(usr_prefix)/$(LIB)
includedir	:= $(usr_prefix)/include
pkgconfdir	:= $(usrlibdir)/pkgconfig
plugindir       := $(prefix)/$(LIB)/multipath
configdir       := $(prefix)/etc/multipath/conf.d
runtimedir      := $(if $(shell test -L /var/run -o ! -d /var/run && echo 1),/run,/var/run)
devmapper_incdir := $(or $(shell $(PKGCONFIG) --variable=includedir devmapper),/usr/include)
libudev_incdir	:= $(or $(shell $(PKGCONFIG) --variable=includedir libudev),/usr/include)
kernel_incdir	:= /usr/include

ifeq ($(V),)
Q		:= @
# make's "Entering directory" messages are confusing in parallel mode
#MAKEFLAGS	= --no-print-directory
endif

GZIP_PROG	:= gzip -9 -c
RM		:= rm -f
LN		:= ln -sf
INSTALL_PROGRAM	:= install

SYSTEMD_CPPFLAGS := $(if $(SYSTEMD),-DUSE_SYSTEMD=$(SYSTEMD))
SYSTEMD_LIBDEPS := $(if $(SYSTEMD),$(if $(shell test $(SYSTEMD) -gt 209 && echo 1),-lsystemd,-lsystemd-daemon))

OPTFLAGS	:= -O2 -g $(STACKPROT) --param=ssp-buffer-size=4
WARNFLAGS	:= -Werror -Wall -Wextra -Wformat=2 $(WFORMATOVERFLOW) -Werror=implicit-int \
		  -Werror=implicit-function-declaration -Werror=format-security \
		  $(WNOCLOBBERED) -Werror=cast-qual $(ERROR_DISCARDED_QUALIFIERS)
CPPFLAGS	:= $(FORTIFY_OPT) \
		   -DBIN_DIR=\"$(bindir)\" -DMULTIPATH_DIR=\"$(plugindir)\" \
		   -DRUNTIME_DIR=\"$(runtimedir)\" \
		   -DCONFIG_DIR=\"$(configdir)\" -DEXTRAVERSION=\"$(EXTRAVERSION)\" -MMD -MP
CFLAGS		:= --std=gnu99 $(CFLAGS) $(OPTFLAGS) $(WARNFLAGS) -pipe
BIN_CFLAGS	:= -fPIE -DPIE
LIB_CFLAGS	:= -fPIC
SHARED_FLAGS	:= -shared
LDFLAGS		:= $(LDFLAGS) -Wl,-z,relro -Wl,-z,now -Wl,-z,defs
BIN_LDFLAGS	:= -pie

# Source code directories. Don't modify.

multipathdir	:= $(TOPDIR)/libmultipath
daemondir	:= $(TOPDIR)/multipathd
mpathutildir	:= $(TOPDIR)/libmpathutil
mpathpersistdir	:= $(TOPDIR)/libmpathpersist
mpathcmddir	:= $(TOPDIR)/libmpathcmd
mpathvaliddir	:= $(TOPDIR)/libmpathvalid
thirdpartydir	:= $(TOPDIR)/third-party
libdmmpdir	:= $(TOPDIR)/libdmmp
nvmedir		:= $(TOPDIR)/libmultipath/nvme

# Common code for libraries - library Makefiles just set DEVLIB
# SONAME defaults to 0 (we use version scripts)
SONAME := 0
LIBS = $(DEVLIB).$(SONAME)
VERSION_SCRIPT = $(DEVLIB:%.so=%.version)
NV_VERSION_SCRIPT = $(DEVLIB:%.so=%-nv.version)

%.o:	%.c
	@echo building $@ because of $?
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.abi:  %.so.0
	$(Q)abidw $< >$@

%.abi:  %.so
	$(Q)abidw $< >$@

%-nv.version:	%.version
	@echo creating $@ from $<
	@printf 'NOVERSION {\nglobal:\n' >$@
	@grep -P '^[ \t]+[a-zA-Z_][a-zA-Z0-9_]*;' $< >>$@
	@printf 'local:\n\t*;\n};\n' >>$@

