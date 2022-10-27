#
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

ifeq ($(shell test -d /lib64 && echo 1),1)
	LIB=lib64
else
	LIB=lib
endif

ifeq ($(shell test -L /var/run -o ! -d /var/run && echo 1),1)
	RUN=run
else
	RUN=var/run
endif

ifeq ($(shell $(PKGCONFIG) --modversion libsystemd >/dev/null 2>&1 && echo 1), 1)
	SYSTEMD = $(shell $(PKGCONFIG) --modversion libsystemd | awk '{print $$1}')
else
	ifeq ($(shell systemctl --version >/dev/null 2>&1 && echo 1), 1)
		SYSTEMD = $(shell systemctl --version 2> /dev/null | \
			sed -n 's/systemd \([0-9]*\).*/\1/p')
	endif
endif

ifeq ($(shell $(PKGCONFIG) --modversion devmapper >/dev/null 2>&1 && echo 1), 1)
	DEVMAPPER_INCDIR = $(shell $(PKGCONFIG) --variable=includedir devmapper)
else
	DEVMAPPER_INCDIR = /usr/include
endif

ifeq ($(shell $(PKGCONFIG) --modversion libudev >/dev/null 2>&1 && echo 1), 1)
	LIBUDEV_INCDIR = $(shell $(PKGCONFIG) --variable=includedir libudev)
else
	LIBUDEV_INCDIR = /usr/include
endif

# Allow user to override default location.
LINUX_HEADERS_INCDIR = /usr/include

# Paths. All these can be overridden on the "make" command line.
prefix		=
# Prefix for binaries
exec_prefix	= $(prefix)
# Prefix for non-essential libraries (libdmmp)
usr_prefix	= $(prefix)
# Where to install systemd-related files. systemd is usually installed under /usr
# Note: some systemd installations use separate "prefix" and "rootprefix".
# In this case, override only unitdir to use systemd's "rootprefix" instead of $(systemd_prefix)
systemd_prefix  := /usr
unitdir		:= $(systemd_prefix)/lib/systemd/system
tmpfilesdir	:= $(systemd_prefix)/lib/tmpfiles.d
modulesloaddir	:= $(systemd_prefix)/lib/modules-load.d
libudevdir	:= $(systemd_prefix)/lib/udev
udevrulesdir	:= $(libudevdir)/rules.d
bindir		= $(exec_prefix)/sbin
multipathdir	= $(TOPDIR)/libmultipath
daemondir       = $(TOPDIR)/multipathd
mpathutildir	= $(TOPDIR)/libmpathutil
man8dir		= $(usr_prefix)/share/man/man8
man5dir		= $(usr_prefix)/share/man/man5
man3dir		= $(usr_prefix)/share/man/man3
syslibdir	= $(prefix)/$(LIB)
usrlibdir	= $(usr_prefix)/$(LIB)
libdir		= $(prefix)/$(LIB)/multipath
mpathpersistdir	= $(TOPDIR)/libmpathpersist
mpathcmddir	= $(TOPDIR)/libmpathcmd
mpathvaliddir	= $(TOPDIR)/libmpathvalid
thirdpartydir	= $(TOPDIR)/third-party
libdmmpdir	= $(TOPDIR)/libdmmp
nvmedir		= $(TOPDIR)/libmultipath/nvme
includedir	= $(usr_prefix)/include
pkgconfdir	= $(usrlibdir)/pkgconfig
plugindir       := $(prefix)/$(LIB)/multipath
configdir       := $(prefix)/etc/multipath/conf.d
runtimedir      := /$(RUN)

GZIP_PROG	= gzip -9 -c
RM		= rm -f
LN		= ln -sf
INSTALL_PROGRAM	= install
NV_VERSION_SCRIPT = $(VERSION_SCRIPT:%.version=%-nv.version)

# $(call TEST_CC_OPTION,option,fallback)
# Test if the C compiler supports the option.
# Evaluates to "option" if yes, and "fallback" otherwise.
TEST_CC_OPTION = $(shell \
	if echo 'int main(void){return 0;}' | \
		$(CC) -o /dev/null -c -Werror "$(1)" -xc - >/dev/null 2>&1; \
	then \
		echo "$(1)"; \
	else \
		echo "$(2)"; \
	fi)

# "make" on some distros will fail on explicit '#' or '\#' in the program text below
__HASH__ := \#
# Check if _DFORTIFY_SOURCE=3 is supported.
# On some distros (e.g. Debian Buster) it will be falsely reported as supported
# but it doesn't seem to make a difference wrt the compilation result.
FORTIFY_OPT := $(shell \
	if /bin/echo -e '$(__HASH__)include <string.h>\nint main(void) { return 0; }' | \
		$(CC) -o /dev/null -c -O2 -Werror -D_FORTIFY_SOURCE=3 -xc - 2>/dev/null; \
	then \
		echo "-D_FORTIFY_SOURCE=3"; \
	else \
		echo "-D_FORTIFY_SOURCE=2"; \
	fi)

STACKPROT := $(call TEST_CC_OPTION,-fstack-protector-strong,-fstack-protector)
ERROR_DISCARDED_QUALIFIERS := $(call TEST_CC_OPTION,-Werror=discarded-qualifiers,)
WNOCLOBBERED := $(call TEST_CC_OPTION,-Wno-clobbered -Wno-error=clobbered,)
WFORMATOVERFLOW := $(call TEST_CC_OPTION,-Wformat-overflow=2,)

OPTFLAGS	:= -O2 -g $(STACKPROT) --param=ssp-buffer-size=4
WARNFLAGS	:= -Werror -Wall -Wextra -Wformat=2 $(WFORMATOVERFLOW) -Werror=implicit-int \
		  -Werror=implicit-function-declaration -Werror=format-security \
		  $(WNOCLOBBERED) -Werror=cast-qual $(ERROR_DISCARDED_QUALIFIERS)
CPPFLAGS	:= $(FORTIFY_OPT) \
		   -DBIN_DIR=\"$(bindir)\" -DMULTIPATH_DIR=\"$(plugindir)\" -DRUN_DIR=\"${RUN}\" \
		   -DRUNTIME_DIR=\"$(runtimedir)\" \
		   -DCONFIG_DIR=\"$(configdir)\" -DEXTRAVERSION=\"$(EXTRAVERSION)\" -MMD -MP
CFLAGS		:= --std=gnu99 $(CFLAGS) $(OPTFLAGS) $(WARNFLAGS) -pipe
BIN_CFLAGS	= -fPIE -DPIE
LIB_CFLAGS	= -fPIC
SHARED_FLAGS	= -shared
LDFLAGS		:= $(LDFLAGS) -Wl,-z,relro -Wl,-z,now -Wl,-z,defs
BIN_LDFLAGS	= -pie

# Check whether a function with name $1 has been declared in header file $2.
check_func = $(shell \
	if grep -Eq "^[^[:blank:]]+[[:blank:]]+$1[[:blank:]]*(.*)*" "$2"; then \
		found=1; \
		status="yes"; \
	else \
		found=0; \
		status="no"; \
	fi; \
	echo 1>&2 "Checking for $1 in $2 ... $$status"; \
	echo "$$found" \
	)

# Checker whether a file with name $1 exists
check_file = $(shell \
	if [ -f "$1" ]; then \
		found=1; \
		status="yes"; \
	else \
		found=0; \
		status="no"; \
	fi; \
	echo 1>&2 "Checking if $1 exists ... $$status"; \
	echo "$$found" \
	)

# Check whether a file contains a variable with name $1 in header file $2
check_var = $(shell \
	if grep -Eq "(^|[[:blank:]])$1([[:blank:]]|=|$$)" "$2"; then \
                found=1; \
                status="yes"; \
        else \
                found=0; \
                status="no"; \
        fi; \
        echo 1>&2 "Checking for ..  $1 in $2 ... $$status"; \
        echo "$$found" \
        )

%.o:	%.c
	@echo building $@ because of $?
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.abi:  %.so.0
	abidw $< >$@

%.abi:  %.so
	abidw $< >$@
