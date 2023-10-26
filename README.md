[![basic-build-and-ci](https://github.com/openSUSE/multipath-tools/actions/workflows/build-and-unittest.yaml/badge.svg)](https://github.com/openSUSE/multipath-tools/actions/workflows/build-and-unittest.yaml) [![compile and unit test on native arch](https://github.com/openSUSE/multipath-tools/actions/workflows/native.yaml/badge.svg)](https://github.com/openSUSE/multipath-tools/actions/workflows/native.yaml) [![compile and unit test on foreign arch](https://github.com/openSUSE/multipath-tools/actions/workflows/foreign.yaml/badge.svg)](https://github.com/openSUSE/multipath-tools/actions/workflows/foreign.yaml)

multipath-tools for Linux
=========================

https://github.com/opensvc/multipath-tools

This package provides the following binaries to drive the Device Mapper multipathing driver:

* multipath - Device mapper target autoconfig.
* multipathc - Interactive client for multipathd.
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


Building multipath-tools
========================

Prerequisites: development packages of for `libdevmapper`, `libaio`, `libudev`,
`libjson-c`, `liburcu`, and `libsystemd`. If commandline editing is enabled
(see below), the development package for either `libedit` or `libreadline` is
required as well.

Then, build and install multipath-tools with:

    make
	make DESTDIR="/my/target/dir" install

To uninstall, type:

    make uninstall

By default, the build will run quietly, just printing one-line messages
about the files being built. To enable more verbose output, run `make V=1`.

Customizing the build
---------------------

**Note**: With very few exceptions, the build process does not read
configuration from the environment. So using syntax like

    SOME_VAR=some_value make

will **not** have the intended effect. Use the following instead:

    make SOME_VAR=some_value

See "Passing standard compiler flags" below for an exception.
The following variables can be passed to the `make` command line:

 * `V=1`: enable verbose build.
 * `plugindir="/some/path"`: directory where libmultipath plugins (path
   checkers, prioritizers, and foreign multipath support) will be looked up.
   This used to be the run-time option `multipath_dir` in earlier versions.
   The default is `$(prefix)/$(LIB)/multipath`, where `$(LIB)` is `lib64` on
   systems that have `/lib64`, and `lib` otherwise.
 * `configfile="/some/path`": The path to the main configuration file.
    The default is `$(etc_prefix)/etc/multipath.conf`.
 * `configdir="/some/path"` : directory to search for additional configuration files.
    This used to be the run-time option `config_dir` in earlier versions.
	The default is `$(etc_prefix)/etc/multipath/conf.d`.
 * `statedir="/some/path"`: The path of the directory where multipath-tools
    stores run-time settings that need persist between reboots, such as known
	WWIDs, user-friendly names, and persistent reservation keys.
	The default is `$(etc_prefix)/etc/multipath`.
 * `READLINE=libedit` or `READLINE=libreadline`: enable command line history
    and TAB completion in the interactive mode *(which is entered with `multipathd -k` or `multipathc`)*.
    The respective development package will be required for building.
    By default, command line editing is disabled.
    Note that using libreadline may
    [make binary indistributable due to license incompatibility](https://github.com/opensvc/multipath-tools/issues/36).
 * `ENABLE_LIBDMMP=0`: disable building libdmmp
 * `ENABLE_DMEVENTS_POLL=0`: disable support for the device-mapper event
   polling API. For use with pre-5.0 kernels that don't support dmevent polling
   (but even if you don't use this option, multipath-tools will work with
   these kernels).
 * `SYSTEMD`: The version number of systemd (e.g. "244") to compile the code for.
   The default is autodetected, assuming that the systemd version in the build
   environment is the same as on the target system. Override the value to
   build for a different systemd version, or set it to `""` to build for a
   system without systemd.
   **Caution:** multipathd without systemd has been largely untested by the
   upstream maintainers since at least 2020.
 * `SCSI_DH_MODULES_PRELOAD="(list)"`: specify a space-separated list of SCSI
   device handler kernel modules to load early during boot. Some
   multipath-tools functionality depends on these modules being loaded
   early. This option causes a *modules-load.d(5)* configuration file to be
   created, thus it depends on functionality provided by *systemd*.
   This variable only matters for `make install`.
   
   **Note**: The usefulness of the preload list depends on the kernel configuration.
   It's especially useful if `scsi_mod` is builtin but `scsi_dh_alua` and
   other device handler modules are built as modules. If `scsi_mod` itself is compiled
   as a module, it might make more sense to use a module softdep for the same
   purpose by creating a `modprobe.d` file like this:
       
        softdep scsi_mod post: scsi_dh_alua scsi_dh_rdac

### Installation Paths

 * `prefix`: The directory prefix for (almost) all files to be installed.
   "Usr-merged" distributions[^systemd] may want to set this to `/usr`. The
   default is empty (`""`).
 * `usr_prefix`: where to install those parts of the code that aren't necessary
   for booting. Non-usr-merged distributions[^systemd] may want to set this to
   `/usr`. The default is `$(prefix)`.
 * `systemd_prefix`: Prefix for systemd-related files[^systemd]. The default is `/usr`.
 * `etc_prefix`: The prefix for configuration files. "usr-merged"
   distributions with immutable `/usr`[^systemd] may want to set this to
   `""`. The default is `$(prefix)`.
 * `LIB`: the subdirectory under `prefix` where shared libraries will be
   installed. By default, the makefile uses `/lib64` if this directory is
   found on the build system, and `/lib` otherwise.
   
The options `configdir`, `plugindir`, `configfile`, and `statedir` above can
be used for setting individual paths where the `prefix` variables don't provide
sufficient control. See `Makefile.inc` for even more fine-grained control.

[^systemd]: systemd installations up to v254 which have been built with
    `split-usr=true` may use separate `prefixdir` and `rootprefixdir`
    directories, where `prefixdir` is a subdirectory of `rootprefixdir`.
	multipath-tools' `systemd_prefix` corresponds to systemd's `prefixdir`.
	On such distributions, override `unitdir` and `libudevdir` to use systemd's
   `rootprefix`: `make libudevdir=/lib/udev unitdir=/lib/systemd/system`

### Compiler Options

Use `OPTFLAGS` to change optimization-related compiler options;
e.g. `OPTFLAGS="-g -O0"` to disable all optimizations.

### Passing standard compiler flags

Contrary to most other variables, the standard variables `CFLAGS`, 
`CPPFLAGS`, and `LDFLAGS` **must** be passed to **make** via the environment
if they need to be customized:

    CPPFLAGS="-D_SECRET_=secret" make

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


Contributing
============

Please send patches or contributions for general discussion about
multipath tools to the mailing list (see below). You can also create
issues or pull requests on
[GitHub](https://github.com/opensvc/multipath-tools).
You will be asked to send your patches to the mailing list
unless your patch is trivial.

Mailing list
------------

The mailing list for multipath-tools is `dm-devel@lists.linux.dev`.
To subscribe, send an email to `dm-devel+subscribe@lists.linux.dev`.
Mailing list archives are available on
[lore.kernel.org](https://lore.kernel.org/dm-devel/) and
[marc.info](https://marc.info/?l=dm-devel). See also the
[lists.linux.dev home page](https://subspace.kernel.org/lists.linux.dev.html).

When sending patches to the mailing list, please add a `Signed-off-by:`
tag, and add Benjamin Marzinski <bmarzins@redhat.com> and 
Martin Wilck <mwilck@suse.com> to the Cc list.

Staging area
------------

Between releases, the latest reviewed code can be obtained from
[the queue branch](https://github.com/openSUSE/multipath-tools/tree/queue)
in the openSUSE/multipath-tools repository on GitHub. From there,
pull requests for new releases in the master repository are
created roughly every 3 months.

Adding new storage devices
--------------------------

If you want to add special settings for a storage device which is
new on the market, follow the instructions at the top of the
file `libmultipath/hwtable.c`.

Changelog
=========

* pre-0.4.5: https://web.archive.org/web/20070309224034/http://christophe.varoqui.free.fr/wiki/wakka.php?wiki=ChangeLog
* post-0.4.5: https://github.com/opensvc/multipath-tools/commits/master


Maintainer
==========

Christophe Varoqui <christophe.varoqui@opensvc.com>
Device-mapper development mailing list <dm-devel@lists.linux.dev>


Licence
=======

The multipath-tools source code is covered by several different licences.
Refer to the individual source files for details.
Source files which do not specify a licence are shipped under LGPL-2.0
(see `LICENSES/LGPL-2.0`).


ALUA
====
This is a rough guide, consult your storage device manufacturer documentation.

ALUA is supported in some devices, but usually it's disabled by default.
To enable ALUA, the following options should be changed:

- EMC CLARiiON/VNX:
   "Failover Mode" should be changed to "4" or "Active-Active mode(ALUA)-failover mode 4"

- HPE 3PAR, Primera, and Alletra 9000:
   "Host:" should be changed to "Generic-ALUA Persona 2 (UARepLun, SESLun, ALUA)".

- Promise VTrak/Vess:
   "LUN Affinity" and "ALUA" should be changed to "Enable", "Redundancy Type"
   must be "Active-Active".

- LSI/Engenio/NetApp RDAC class, as NetApp SANtricity E/EF Series and rebranded arrays:
   "Select operating system:" should be changed to "Linux DM-MP (Kernel 3.10 or later)".

- NetApp ONTAP FAS/AFF Series:
   To check ALUA state: "igroup show -v <igroup_name>", and to enable ALUA:
   "igroup set <igroup_name> alua yes".

- Huawei OceanStor:
   "Host Access Mode" should be changed to "Asymmetric".


NVMe
====

Using dm-multipath with NVMe
----------------------------

NVMe multipath is natively supported by the Linux kernel. If for some reason
you prefer using device mapper multipath with NVMe devices,
you need to disable native multipathing first:

    echo "options nvme_core multipath=N" > /etc/modprobe.d/01-nvme_core-mp.conf

Afterwards, regenerate the initramfs (`dracut -f` or `update-initramfs`) and reboot.

Using multipath-tools with native NVMe multipath
------------------------------------------------

If native NVMe multipathing is enabled, you can still use multipath-tools
for displaying the topology and some other information about native NVMe
multipath setups. This feature is disabled by default. To enable it, set
`enable_foreign nvme` in the `defaults` section of `multipath.conf`.
Commands like `multipath -ll` will then display information about NVMe
native multipath. This support is read-only; modifying the native multipath
configuration is not supported.
