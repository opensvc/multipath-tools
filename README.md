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

