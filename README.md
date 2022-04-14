[![basic-build-and-ci](https://github.com/openSUSE/multipath-tools/actions/workflows/build-and-unittest.yaml/badge.svg)](https://github.com/openSUSE/multipath-tools/actions/workflows/build-and-unittest.yaml) [![compile and unit test on native arch](https://github.com/openSUSE/multipath-tools/actions/workflows/native.yaml/badge.svg)](https://github.com/openSUSE/multipath-tools/actions/workflows/native.yaml) [![compile and unit test on foreign arch](https://github.com/openSUSE/multipath-tools/actions/workflows/foreign.yaml/badge.svg)](https://github.com/openSUSE/multipath-tools/actions/workflows/foreign.yaml)

multipath-tools for Linux
=========================


https://github.com/opensvc/multipath-tools

This package provides the following binaries to drive the Device Mapper multipathing driver:

* multipath - Device mapper target autoconfig.
* multipathd - Multipath daemon.
* mpathpersist - Manages SCSI persistent reservations on dm multipath devices.
* kpartx - Create device maps from partition tables.


Releases
========

To get a specific X.Y.Z release, use one of the following method:


Git
---

    git clone https://github.com/opensvc/multipath-tools.git
    cd multipath-tools
    git tag
    git archive --format=tar.gz --prefix=multipath-tools-X.Y.Z/ X.Y.Z > ../multipath-tools-X.Y.Z.tar.gz


Direct download
---------------

    wget "https://github.com/opensvc/multipath-tools/archive/X.Y.Z.tar.gz" -O multipath-tools-X.Y.Z.tar.gz


Browser
-------

Go to: https://github.com/opensvc/multipath-tools/tags
Select a release-tag and then click on "zip" or "tar.gz".


Source code
===========

To get latest devel code:

    git clone https://github.com/opensvc/multipath-tools.git

Github page: https://github.com/opensvc/multipath-tools


Building multipath-tools
========================

Prerequisites: development packages of for `libdevmapper`, `libreadline`,
`libaio`, `libudev`, `libjson-c`, `liburcu`, and `libsystemd`.

To build multipath-tools, type:

    make
	make DESTDIR="/my/target/dir" install

To uninstall, type:

    make uninstall

Customizing the build
---------------------

The following variables can be passed to the `make` command line:

 * `plugindir="/some/path"`: directory where libmultipath plugins (path
   checkers, prioritizers, and foreign multipath support) will be looked up.
   This used to be the run-time option `multipath_dir` in earlier versions.
 * `ENABLE_LIBDMMP=0`: disable building libdmmp
 * `ENABLE_DMEVENTS_POLL=0`: disable support for the device-mapper event
   polling API. For use with pre-5.0 kernels that don't supprt dmevent polling
   (but even if you don't use this option, multipath-tools will work with
   these kernels).
 * `SCSI_DH_MODULES_PRELOAD="(list)"`: specify a space-separated list of SCSI
   device handler kernel modules to load early during boot. Some
   multipath-tools functionality depends on these modules being loaded
   early. This option causes a *modules-load.d(5)* configuration file to be
   created, thus it depends on functionality provided by *systemd*.
   This variable only matters for `make install`.

Note: The usefulness of the preload list depends on the kernel configuration.
It's especially useful if `scsi_mod` is builtin but `scsi_dh_alua` and
other device handler modules are built as modules. If `scsi_mod` itself is compiled
as a module, it might make more sense to use a module softdep for the same
purpose.

See `Makefile.inc` for additional variables to customize paths and compiler
flags.

Special Makefile targets
------------------------

The following targets are intended for developers only.

 * `make test` to build and run the unit tests
 * `make valgrind-test` to run the unit tests under valgrind
 * `make abi` to create an XML representation of the ABI of the libraries in
   the `abi/` subdirectory
 * `make abi-test` to compare the ABI of a different multipath-tools version,
   which must be stored in the `reference-abi/` subdirectory. If this test
   fails, the ABI has changed wrt the reference.
 * `make compile-commands.json` to create input for [clangd](https://clangd.llvm.org/).


Add storage devices
===================

Follow the instructions in the `libmultipath/hwtable.c` header.


Mailing list
============

(subscribers-only)
To subscribe and archives: https://www.redhat.com/mailman/listinfo/dm-devel
Searchable: https://marc.info/?l=dm-devel


Changelog
=========

pre-0.4.5: https://web.archive.org/web/20070309224034/http://christophe.varoqui.free.fr/wiki/wakka.php?wiki=ChangeLog
post-0.4.5: https://github.com/opensvc/multipath-tools/commits/master


Maintainer
==========

Christophe Varoqui <christophe.varoqui@opensvc.com>
Device-mapper development mailing list <dm-devel@redhat.com>


Licence
=======

The multipath-tools source code is covered by several different licences.
Refer to the individual source files for details.
Source files which do not specify a licence are shipped under LGPL-2.0
(see `LICENSES/LGPL-2.0`).

