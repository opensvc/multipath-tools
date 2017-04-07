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
# Uncomment to disable RADOS support (e.g. if rados headers are missing).
# ENABLE_RADOS = 0
#
# Uncomment to disable libdmmp support
# ENABLE_LIBDMMP = 0

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
	ifeq ($(shell systemctl --version > /dev/null 2>&1 && echo 1), 1)
		SYSTEMD = $(shell systemctl --version 2> /dev/null |  sed -n 's/systemd \([0-9]*\)/\1/p')
	endif
endif

ifndef SYSTEMDPATH
	SYSTEMDPATH=usr/lib
endif

prefix		=
exec_prefix	= $(prefix)
bindir		= $(exec_prefix)/sbin
libudevdir	= $(prefix)/$(SYSTEMDPATH)/udev
udevrulesdir	= $(libudevdir)/rules.d
multipathdir	= $(TOPDIR)/libmultipath
man8dir		= $(prefix)/usr/share/man/man8
man5dir		= $(prefix)/usr/share/man/man5
man3dir		= $(prefix)/usr/share/man/man3
syslibdir	= $(prefix)/$(LIB)
libdir		= $(prefix)/$(LIB)/multipath
unitdir		= $(prefix)/$(SYSTEMDPATH)/systemd/system
mpathpersistdir	= $(TOPDIR)/libmpathpersist
mpathcmddir	= $(TOPDIR)/libmpathcmd
thirdpartydir	= $(TOPDIR)/third-party
libdmmpdir	= $(TOPDIR)/libdmmp
includedir	= $(prefix)/usr/include
pkgconfdir	= $(prefix)/usr/share/pkgconfig

GZIP		= gzip -9 -c
RM		= rm -f
LN		= ln -sf
INSTALL_PROGRAM	= install

OPTFLAGS	= -O2 -g -pipe -Wall -Wextra -Wformat=2 -Werror=implicit-int \
		  -Werror=implicit-function-declaration -Werror=format-security \
		  -Wno-sign-compare -Wno-unused-parameter -Wno-clobbered \
		  -Wp,-D_FORTIFY_SOURCE=2 -fstack-protector-strong \
		  --param=ssp-buffer-size=4

CFLAGS		= $(OPTFLAGS) -fPIC -DLIB_STRING=\"${LIB}\" -DRUN_DIR=\"${RUN}\"
SHARED_FLAGS	= -shared

# Check whether a function with name $1 has been declared in header file $2.
check_func =								       \
    $(shell								       \
	if grep -Eq "^[^[:blank:]]+[[:blank:]]+$1[[:blank:]]*(.*)*" "$2"; then \
	   found=1;							       \
	   status="yes";						       \
	else								       \
	   found=0;							       \
	   status="no";							       \
	fi;								       \
	echo 1>&2 "Checking for $1 in $2 ... $$status";			       \
	echo "$$found"							       \
    )

%.o:	%.c
	$(CC) $(CFLAGS) -c -o $@ $<
