#
# Copyright (C) 2004 Christophe Varoqui, <christophe.varoqui@opensvc.com>
#

#
# Allow to force some libraries to be used statically. (Uncomment one of the
# following lines or define the values when calling make.)
#
# WITH_LOCAL_LIBDM	= 1
# WITH_LOCAL_LIBSYSFS	= 1
#
# Uncomment to disable libdmmp support
# ENABLE_LIBDMMP = 0
#
# Uncomment to disable dmevents polling support
# ENABLE_DMEVENTS_POLL = 0

ifeq ($(TOPDIR),)
	TOPDIR	= ..
endif

ifndef LIB
	ifeq ($(shell test -d /lib64 && echo 1),1)
		LIB=lib64
	else
		LIB=lib
	endif
endif

ifndef RUN
	ifeq ($(shell test -L /var/run -o ! -d /var/run && echo 1),1)
		RUN=run
	else
		RUN=var/run
	endif
endif

ifndef SYSTEMD
	ifeq ($(shell pkg-config --modversion libsystemd >/dev/null 2>&1 && echo 1), 1)
		SYSTEMD = $(shell pkg-config --modversion libsystemd)
	else
		ifeq ($(shell systemctl --version >/dev/null 2>&1 && echo 1), 1)
			SYSTEMD = $(shell systemctl --version 2> /dev/null | \
				sed -n 's/systemd \([0-9]*\).*/\1/p')
		endif
	endif
endif

ifndef SYSTEMDPATH
	SYSTEMDPATH=usr/lib
endif

prefix		=
exec_prefix	= $(prefix)
usr_prefix	= $(prefix)
bindir		= $(exec_prefix)/sbin
libudevdir	= $(prefix)/$(SYSTEMDPATH)/udev
udevrulesdir	= $(libudevdir)/rules.d
multipathdir	= $(TOPDIR)/libmultipath
man8dir		= $(prefix)/usr/share/man/man8
man5dir		= $(prefix)/usr/share/man/man5
man3dir		= $(prefix)/usr/share/man/man3
syslibdir	= $(prefix)/$(LIB)
usrlibdir	= $(usr_prefix)/$(LIB)
libdir		= $(prefix)/$(LIB)/multipath
unitdir		= $(prefix)/$(SYSTEMDPATH)/systemd/system
mpathpersistdir	= $(TOPDIR)/libmpathpersist
mpathcmddir	= $(TOPDIR)/libmpathcmd
thirdpartydir	= $(TOPDIR)/third-party
libdmmpdir	= $(TOPDIR)/libdmmp
nvmedir		= $(TOPDIR)/libmultipath/nvme
includedir	= $(prefix)/usr/include
pkgconfdir	= $(usrlibdir)/pkgconfig

GZIP		= gzip -9 -c
RM		= rm -f
LN		= ln -sf
INSTALL_PROGRAM	= install

# $(call TEST_CC_OPTION,option,fallback)
# Test if the C compiler supports the option.
# Evaluates to "option" if yes, and "fallback" otherwise.
TEST_CC_OPTION = $(shell \
	if echo 'int main(void){return 0;}' | $(CC) -o /dev/null -c "$(1)" -xc - >/dev/null 2>&1; \
	then \
		echo "$(1)"; \
	else \
		echo "$(2)"; \
	fi)

STACKPROT := $(call TEST_CC_OPTION,-fstack-protector-strong,-fstack-protector)
ERROR_DISCARDED_QUALIFIERS := $(call TEST_CC_OPTION,-Werror=discarded-qualifiers,)

OPTFLAGS	= -O2 -g -pipe -Wall -Wextra -Wformat=2 -Werror=implicit-int \
		  -Werror=implicit-function-declaration -Werror=format-security \
		  -Wno-sign-compare -Wno-unused-parameter -Wno-clobbered \
		  -Werror=cast-qual $(ERROR_DISCARDED_QUALIFIERS) \
		  -Wp,-D_FORTIFY_SOURCE=2 $(STACKPROT) \
		  --param=ssp-buffer-size=4

CFLAGS		:= $(OPTFLAGS) -DBIN_DIR=\"$(bindir)\" -DLIB_STRING=\"${LIB}\" -DRUN_DIR=\"${RUN}\" \
		   -MMD -MP $(CFLAGS)
BIN_CFLAGS	= -fPIE -DPIE
LIB_CFLAGS	= -fPIC
SHARED_FLAGS	= -shared
LDFLAGS		= -Wl,-z,relro -Wl,-z,now
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

%.o:	%.c
	@echo building $@ because of $?
	$(CC) $(CFLAGS) -c -o $@ $<
