# multipath-tools Release Notes

## Stable branches and new versioning scheme

Beginning with 0.11, the second digit in the multipath-tools version will be
used for upstream "major" releases. The 3rd and last digit will denote stable
releases in the future, containing only bug fixes on top of the last major
release. These bug fixes will be tracked in stable branches.

See [README.md](README.md) for additional information.

## multipath-tools 0.11.1, 2025/02

This release contains backported bug fixes from the master branch up to 0.12.

### Bug fixes

* Fix multipathd crash because of invalid path group index value, for example
  if an invalid path device was removed from a map.
  Fixes [#105](https://github.com/opensvc/multipath-tools/issues/105).
* Make sure maps are reloaded in the path checker loop after detecting an
  inconsistent or wrong kernel state (e.g. missing or falsely mapped path
  device). Wrongly mapped paths will be unmapped and released to the system.
  Fixes another issue reported in
  [#105](https://github.com/opensvc/multipath-tools/issues/105).
* Fix the problem that `group_by_tpg` might be disabled if one or more
  paths were offline during initial configuration.
* Fix possible misdetection of changed pathgroups in a map.
* Fix the problem that if a map was scheduled to be reloaded already,
  `max_sectors_kb` might not be set on a path device that
  was being added to a multipath map. This problem was introduced in 0.9.9.

## multipath-tools 0.11.0, 2024/11

### User-visible changes

* Modified the systemd unit `multipathd.service` such that multipathd will now
  restart after a failure or crash.
  Fixes [#100](https://github.com/opensvc/multipath-tools/issues/100).
* Logging changes for verbosity level 3:
  - silenced logging of path status if the status is unchanged
  - silenced some unhelpful messages from scanning of existing maps
  - added a message when partition mappings are removed.

### Other major changes

#### Rework of the path checking algorithm

This is a continuation of the checker-related work that went into 0.10.0. For
asynchronous checker algorithms (i.e. tur and directio), the start of the
check and the retrieval of the results are done at separate points in time,
which reduces the time for waiting for the checker results of individual paths
and thus strongly improves the performance of the checker algorithm, in
particular on systems with a large a amount of paths.

The algorithm has the following additional properties:

1. multipath devices get synchronized with the kernel occasionally, even if
they have not paths
2. If multiple paths from a multipath device are checked in the same
loop, the multipath device will only get synchronized with the kernel once.
3. All the paths of a multipath device will converge to being checked at
the same time (at least as much as their differing checker intervals  will
allow).
4. The times for checking the paths of each multipath device will spread
out as much as possible so multipathd doesn't do all of it's checking in
a burst.
5. path checking is done by multipath device (for initialized paths,
the uninitialized paths are handled after all the adopted paths are
handled).

### Bug fixes

* Fixed the problem that multipathd wouldn't start on systems with certain types
  of device mapper devices, in particular devices with multiple DM targets.
  The problem was introduced in 0.10.0.
  Fixes [#102](https://github.com/opensvc/multipath-tools/issues/102).
* Fixed a corner case in the udev rules which could cause a device not to be
  activated during boot if a cold plug uevent is processed for a previously
  not configured multipath map while this map was suspended. This problem existed
  since 0.9.8.
* Fixed the problem that devices with `no_path_retry fail` and no setting
  for `dev_loss_tmo` might get the `dev_loss_tmo` set to 0, causing the
  device to be deleted immediately in the event of a transport disruption.
  This bug was introduced in 0.9.6.
* Fixed the problem that, if there were multiple maps with deferred failback
  (`failback` value > 0 in `multipath.conf`), some maps might fail back later
  than configured. The problem existed since 0.9.6.
* Fixed handling of empty maps, i.e. multipath maps that have a multipath UUID
  but don't contain a device-mapper table. Such maps can occur in very rare
  cases if some device-mapper operation has failed, or if a tool has been
  killed in the process of map creation. multipathd will now detect such
  cases, and either remove these maps or reload them as appropriate.
* During map creation, fixed the case where a map with different name, but
  matching UUID and matching type was already present. multipathd
  previously failed to set up such maps. Now it will reload them with the
  correct content.
* Fixed the logic for enabling the systemd watchdog (`WatchdogSec=` in the
  systemd unit file for multipathd).
* Fixed a memory leak in the nvme foreign library. The bug existed since
  0.7.8.
* Fixed a problem in the marginal path detection algorithm that could cause
  the io error check for a recently failed path to be delayed. This bug
  existed since 0.7.4.

### Other

* Default settings for `hardware_handler` have been removed from the
  internal hardware table. These settings have been obsoleted by the Linux
  kernel 4.3 and later, which automatically selects hardware handlers when
  SCSI devices are added. See the notes about `SCSI_DH_MODULES_PRELOAD` in
  [README.md](README.md).
* Added a hardware table entry for the SCSI Target Subsystem for Linux (SCST).
* The text of the licenses has been updated to the latest versions from the
  Free Software Foundation.

### Internal

* `libmp_mapinfo()` now fills in the `name`, `uuid`, and `dmi` fields
  if requested by the caller, even if it encounters an error or an empty map.

## multipath-tools 0.10.0, 2024/08

### User-Visible Changes

* The `multipathd show daemon` command now shows `(reconfigure pending)`
  if a reconfiguration has been triggered but not finished yet.

### Other major changes

* Refactored the path checker loop. Paths are now checked for each multipath
  map in turn, rather than walking linearly through the list of paths. Paths
  for different multipath maps will be checked at different time offsets in
  the `polling_interval` time span, which distributes the load caused by
  path checking more evenly over time.
* Refactored a significant part of the libmultipath / libdevmapper interface.
  All functions that retrieve information about DM maps have been converted
  to use just one worker function, libmp_mapinfo(). This reduces code size
  while providing more flexibility and efficiency (less device-mapper ioctls).
  Also, cleanup attributes are used consistently in the libdevmapper-related code.
* Renamed public functions, variables, and macros to comply with the
  glibc [policy for reserved names](https://www.gnu.org/software/libc/manual/html_node/Reserved-Names.html).
  For backward compatibility reasons, the exported functions  from `libmpathcmd`
  and `libmpathpersist` that start with double underscore are kept as weak
  symbols. Fixes [#91](https://github.com/opensvc/multipath-tools/issues/91).

### Bug fixes

* Fixed bug that caused queueing to be always disabled if flushing a map failed
  (bug introduced in 0.9.8).
* Fixed failure to remove maps even with `deferred_remove` (bug introduced in
  0.9.9).
* Fixed old mpathpersist bug leading to the error message "configured reservation
  key doesn't match: 0x0" when `reservation_key` was configured in the
  multipaths section of `multipath.conf`
  (Fixes [#92](https://github.com/opensvc/multipath-tools/issues/92)).
* Fixed output of `multipath -t` and `multipath -T` for the options
  `force_sync` and `retrigger_tries`.
  (Fixes [#88](https://github.com/opensvc/multipath-tools/pull/88))
* Fixed adding maps by WWID in CLI (command `add map $WWID`).

### Other

* Removed hardcoded paths and make them configurable instead.
  This should improve compatibility e.g. with NixOS.
* Improved handling of paths with changed WWIDs.
* Improved synchronization between kernel state and multipathd's internal
  state.
* Made map removal more efficient by avoiding unnecessary recursion.
* Added hardware defaults for Huawei storage arrays and XSG1 vendors.
* Use `-fexceptions` during compilation to make sure cleanup code is executed
  when threads are cancelled
* Use `weak` attribute for `get_multipath_config()` and
  `put_multipath_config()` in order to enable linking with
  `-Bsymbolic-non-weak-functions`
  (Fixes [#86](https://github.com/opensvc/multipath-tools/pull/86)).
* Fixed CI for ARM/v7
* Fixed directio CI test for real devices, run more "real" tests in CI
* Fixed minor issues detected by coverity.
* Fixed a minor bug in the config file parser
  (Fixes [#93](https://github.com/opensvc/multipath-tools/pull/93)).
* Minor documentation fixes
  (Fixes [#87](https://github.com/opensvc/multipath-tools/pull/87)).

## multipath-tools 0.9.9, 2024/05

### Important note

It is not recommended to use *lvm2* 2.03.24 and newer with multipath-tools
versions earlier than 0.9.9. See "Other major changes" below.

### User-Visible Changes

* *Changed realtime scheduling:* multipathd used to run at the highest possible
  realtime priority, 99. This has always been excessive, and on some
  distributions (e.g. RHEL 8), it hasn't worked at all.  It is now possible to
  set multipathd's real time scheduling by setting the hard limit for
  `RLIMIT_RTPRIO` (see getrlimit(2)), which corresponds to the `rtprio`
  setting in limits.conf and to `LimitRTPRIO=` in the systemd unit file. The
  default in the systemd unit file has been set to 10.  If the limit is set to
  0, multipathd doesn't attempt to enable real-time scheduling.
  Otherwise, it will try to set the scheduling priority to the given value.
  Fixes [#82](https://github.com/opensvc/multipath-tools/issues/82).
* *Changed normal scheduling:* In order to make sure that multipathd has
  sufficient priority even if real time scheduling is switched off, the
  `CPUWeight=` setting in the unit file is set to 1000. This is necessary
  because regular nice(2) values have no effect in systems with cgroups enabled.
* *Changed handling of `max_sectors_kb` configuration:* multipathd applies
  the `max_sectors_kb` setting only during map creation, or when a new path is
  added to an existing map. The kernel makes sure that the multipath device
  never has a larger `max_sectors_kb` value than any of its configured path
  devices. The reason for this change is that applying `max_sectors_kb` on
  live maps can cause IO errors and data loss in rare situations.
  It can now happen that some path devices have a higher `max_sectors_kb`
  value than the map; this is not an error. It is not possible any more to
  decrease `max_sectors_kb` in `multipath.conf` and run `multipathd
  reconfigure` to "apply" this setting to the map and its paths. If decreasing
  the IO size is necessary, either destroy and recreate the map, or remove one
  path with `multipathd del path $PATH`, run `multipathd reconfigure`, and
  re-add the path with `multipathd add path $PATH`.
* *New wildcard %k:* The wildcard `%k` for `max_sectors_kb` has been added to
   the `multipathd show paths format` and `multipathd show maps format`
   commands.
* *Changed semantics of flush_on_last_del:* The `flush_on_last_del` option
   now takes the values `always` , `unused`, or `never`. `yes` and `no`
   are still accepted as aliases for `always` and `unused`, respectively.
   `always` means that when all paths for a multipath map have been removed,
   *outstanding IO will be failed* and the map will be deleted. `unused` means
   that this will only happen when the map is not mounted or otherwise opened.
   `never` means the map will only be removed if the `queue_if_no_path`
   feature is off.
   This fixes a problem where multipathd could hang when the last path of 
   a queueing map was deleted.
* *Better parsing of `$map` arguments in multipathd interactive shell*: The
  `$map` argument in commands like `resize map $map` now accepts a WWID,
  and poorly chosen map aliases that could be mistaken for device names.
* *Added documentation for CLI wildcards*. The wildcards used in the `show
  maps format` and `show paths format` commands are documented in the
  *multipathd(8)* man page now.
* *`%s` wildcard for paths:* this wildcard in `show paths format` now prints
  the product revision, too.

### Other major changes

* Adapted the dm-mpath udev rules such that they will work with the modified
  device mapper udev rules (`DM_UDEV_RULES_VSN==3`, lvm2 >= 2.03.24). They are
  still compatible with older versions of the device-mapper udev
  rules (lvm2 < 2.03.24). If lvm2 2.03.24 or newer is installed, it is
  recommended to update multipath-tools to 0.9.9 or newer.
  See also [LVM2 2.03.24 release notes](https://gitlab.com/lvmteam/lvm2/-/tags/v2_03_24).

### Bug fixes

* Fixed misspelled DM_UDEV_DISABLE_OTHER_RULES_FLAG in 11-dm-mpath.rules
* Always use `glibc_basename()` to avoid some issues with MUSL libc.
  Fixes [#84](https://github.com/opensvc/multipath-tools/pull/84).
* Fixed map failure counting for `no_path_retry > 0`.
* The wildcards for path groups are not available for actual
  commands and have thus been removed from the `show wildcards` command
  output.

### Other

* Build: added `TGTDIR` option to simplify building for a different target
  host (see README.md).

## multipath-tools 0.9.8, 2024/02

### User-Visible Changes

* Socket activation via multipathd.socket has been disabled by default because
  it has undesirable side effects (fixes
  [#76](https://github.com/opensvc/multipath-tools/issues/76), at least partially).
* The restorequeueing CLI command now only enables queueing if disablequeueing
  had been sent before.
* Error messages sent from multipathd to the command line client have been
  improved. The user will now see messages like "map or partition in use" or
  "device not found" instead of just "fail".

### Other Major Changes

* multipathd now tracks the queueing mode of maps in its internal features
  string. This is helpful to ensure that maps have the desired queuing
  status. Without this, it could happen that a map remains in queueing state
  even after the `no_path_retry` timeout has expired.
* multipathd's map flushing code has been reworked to avoid hangs if there are
  no paths but outstanding IO. Thus, if multipathd is running, `multipath -F`
  can now retry map flushing using the daemon, rather than locally.

### Bug Fixes

* A segmentation fault in the 0.9.7 autoresize code has been fixed.
* Fixed a bug introduced in 0.9.6 that had caused map reloads being omitted
  when path priorities changed.
* Fixed compilation with gcc 14. (Fixes [#80](https://github.com/opensvc/multipath-tools/issues/80))
* Minor fixes for issues detected by coverity.
* Spelling fixes and other minor fixes.

### CI

* Enabled `-D_FILE_OFFSET_BITS=64` to fix issues with emulated 32-bit
  environments in the GitHub CI, so that we can now run our CI in arm/v7.
* Added the check-spelling GitHub action.
* Various improvements and updates for the GitHub CI workflows.

## multipath-tools 0.9.7, 2023/11

### User-Visible Changes

* The options `bindings_file`, `wwids_file` and `prkeys_file`, which were
  deprecated since 0.8.8, have been removed. The path to these files is now
  hard-coded to `$(statedir)` (see below).
* Added `max_retries` config option to limit SCSI retries.
* Added `auto_resize` config option to enable resizing multipath maps automatically.
* Added support for handling FPIN-Li events for FC-NVMe.

### Other Major Changes

* Rework of alias selection code:
  - strictly avoid using an alias that is already taken.
  - cache bindings table in memory.
  - write bindings file only if changes have been applied, and watch it with inotify.
  - sort aliases in "alias order" by using length+alphabetical sort, allowing
    more efficient allocation of new aliases

### Bug Fixes

* Avoid that `multipath -d` changes sysfs settings.
* Fix memory and error handling of code using aio in the marginal paths code.
and the directio checker (fixes
[#73](https://github.com/opensvc/multipath-tools/issues/73)).
* Insert compile time settings for paths in man pages correctly.

### Other

* Add new compile-time variable `statedir` which defaults to `/etc/multipath`.
* Add new compile-time variable `etc_prefix` as prefix for config file and config dir.
* Compile-time variable `usr_prefix` now defaults to `/usr` if `prefix` is empty.
* Remove check whether multipath is enabled in systemd `.wants` directories.
* README improvements.

## multipath-tools 0.9.6, 2023/09

### User-Visible Changes

* Added new path grouping policy `group_by_tpg` to group paths by their ALUA
  target port group (TPG).
* Added new configuration parameters `detect_pgpolicy` (default: yes) and
  `detect_pgpolicy_use_tpg` (default: no).
* Add new wildcard `%A` to print target port group in `list paths format` command.
* NVMe devices are now ignored if NVMe native multipath is enabled in the
  kernel.

### Other Major Changes

* Prioritizers now use the same timeout logic as path checkers.
* Reload maps if the path groups aren't properly ordered by priority.
* Improve logic for updating path priorities.
* Avoid paths with unknown priority affecting the priority of their path
  group.

### Bug Fixes

* Fix `max_sectors_kb` for cases where a path is deleted and re-added
  (Fixes [#66](https://github.com/opensvc/multipath-tools/pull/66)).
* Fix handling of `dev_loss_tmo` in cases where it wasn't explicitly
  configured.
* Syntax fixes in udev rules (Fixes [#69](https://github.com/opensvc/multipath-tools/pull/69)).

### Other

* Adapt HITACHI/OPEN- config to work with alua and multibus.
* Build system fixes.

## multipath-tools 0.9.5, 2023/04

### User-Visible Changes

* Always use directio path checker for Linux TCM (LIO) targets
  (Fixes [#54](https://github.com/opensvc/multipath-tools/issues/54).
* `multipath -u` now checks if path devices are already in use 
  (e.g. mounted), and if so, makes them available to systemd immediately.

### Other Major Changes

* Persistent reservations are now handled consistently. Previously, whether a
  PR key was registered for a path depended on the situation in which the
  path had been first detected by multipathd.

### Bug Fixes

* Make sure that if a map device must be renamed and reloaded, both
  actions actually take place (previously, the map would only be renamed).
* Make sure to always flush IO if a map is resized.
* Avoid incorrectly claiming path devices in `find_multipaths smart` case
  for paths that have no valid WWID or for which `multipath -u` failed.
* Avoid paths failures for ALUA transitioning state
  (fixes [#60]( https://github.com/opensvc/multipath-tools/pull/60).
* Handle persistent reservations correctly even if there are no active paths
  during map creation.
* Make sure all paths are orphaned if maps are removed.
* Avoid error messages for unsupported device designators in VPD pages.
* Fix a memory leak.
* Honor the global option `uxsock_timeout` in libmpathpersist
  (fixes [#45](https://github.com/opensvc/multipath-tools/issues/45)).
* Don't fail for devices lacking INQUIRY properties such as "vendor"
  (fixes [#56](https://github.com/opensvc/multipath-tools/issues/56)).
* Remove `Also=` in `multipathd.socket`
  (fixes [#65](https://github.com/opensvc/multipath-tools/issues/65)).

### CI

* Use Ubuntu 22.04 instead of 18.04.

## multipath-tools 0.9.4, 2022/12

### Bug Fixes

* Verify device-mapper table configuration strings before passing them
  to the kernel.
* Fix failure of `setprstatus`, `unsetprstatus` and `unsetprkey` commands
  sent from libmpathpersist introduced in 0.9.2.
* Fix a memory leak.
* Compilation fixes for some architectures, older compilers, and MUSL libc.
* Fix `show paths format %c` failure for orphan paths
  (fixes [#49](https://github.com/opensvc/multipath-tools/pull/49))

### Build system changes

* Added a simple `autoconf`-like mechanism.
* Use "quiet build" by default, verbose build can be enabled using `make V=1`.
* Reworked the Makefile variables for configuring paths.
* Don't require perl just for installation of man pages.

### CI

* True "multi-architecture" workflows are now possible on GitHub workflows, to
  test compilation and run unit tests on different architectures.
* Containers for test builds are now pulled from ghcr.io rather than from
  docker hub.

### Other

* Updates for the hardware table: PowerMax NVMe, Alletra 5000, FAS/AFF and
  E/EF.
* Documentation fixes.

## multipath-tools 0.9.3, 2022/10

### Bug fixes

* Fix segmentation violation caused by different symbol version numbers in
  libmultipath and libmpathutil 
  (fixes [47](https://github.com/opensvc/multipath-tools/issues/47).

## multipath-tools 0.9.2, 2022/10

### User-Visible Changes
  
* Fix handling of device-mapper `queue_mode` parameter.
* Enforce `queue_mode bio` for NVMe/TCP paths.
  
### Other Major Changes

* Rework the command parsing logic in multipathd (CVE-2022-41974).
* Use `/run` rather than `/dev/shm` (CVE-2022-41973).
* Check transport protocol for NVMe devices.

### Bug Fixes

* Rework feature string handling, fixing bugs for corner cases.
* Fix a race in kpartx partition device creation.
* Fix memory leak in the unix socket listener code.
* Fix a read past end of buffer in the unix socket listener code.
* Fix compilation error with clang 15.

## multipath-tools 0.9.1, 2022/09

### User-Visible Changes

* multipathd doesn't use libreadline any more due to licensing
  conflicts, because readline has changed its license to GPL 3.0,
  which is incompatible with the GPL-2.0-only license of parts of the
  multipath-tools code base.
  Therefore the command line editing feature in multipathd is
  disabled by default. libedit can be used instead of libreadline by
  setting `READLINE=libedit` during compilation. 
  `READLINE=libreadline` can also still be set. Only the new helper program
  *multipathc*, which does not contain GPL-2.0 code, is linked with
  libreadline or libedit. `multipathd -k` now executes `multipathc`.
  Fixes [36](https://github.com/opensvc/multipath-tools/issues/36).
* As part of the work separating code of conflicting licenses, the multipath
  library has been split into `libmultipath` and `libmpathutil`. The latter
  can be linked with GPL-3.0 code without licensing conflicts.
* Speed up start of `multipath -u` and `multipath -U`.
* Speed up seeking for aliases in systems with lots of alias entries.
* Always use the `emc_clariion` checker for Clariion/Unity storage arrays.
  
### Bug Fixes

* Avoid checker thread blocking uevents or other requests for an extended
  amount of time with a huge amount of path devices, by occasionally
  interrupting the checker loop.
* Fix handling the case where a map ended up with no paths while being
  updated.
* Fix a segmentation violation in `list map format` code.
* Fix use-after-free in code handling path WWID changes by sorting the
  alias table.
* Fix timeout handling in unix socket listener code.
* Fix systemd timers in the initramfs.
* Fix `find_multipaths_timeout` for unknown hardware.
* Fix `multipath -ll` output for native NVMe.

### Other

* Cleanup code for sysfs access, and sanitize error handling.
* Separation of public and internal APIs in libmpathpersist.
* Build system fixes.
* Spelling fixes.

## multipath-tools 0.9.0, 2022/06

### User-Visible Changes

 * The properties `dev_loss_tmo`, `eh_deadline`, and `fast_io_fail_tmo` can
   now be set *by protocol*, in the `overrides` → `protocol` section of
   `multipath.conf`.
 * The `config_dir` and `multipath_dir` run-time options, marked deprecated
   since 0.8.8, have been replaced by the build-time options `configdir=` and
   `plugindir=`, respectively.
 * `getuid_callout` is not supported any more.
   
### Other Major Changes
   
 * The uevent filtering and merging code has been re-written to avoid
   artificial delays in uevent processing.
   
### Bug fixes

 * The `delayed_reconfigure` logic has been fixed.

### Other

* hardware table updates.
