/*
  Copyright (c) 2020 Benjamin Marzinski, IBM

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <stddef.h>
#include <errno.h>
#include <libudev.h>

#include "vector.h"
#include "config.h"
#include "debug.h"
#include "util.h"
#include "devmapper.h"
#include "discovery.h"
#include "wwids.h"
#include "sysfs.h"
#include "blacklist.h"
#include "mpath_cmd.h"
#include "valid.h"

int
is_path_valid(const char *name, struct config *conf, struct path *pp,
	      bool check_multipathd)
{
	int r;
	int fd;

	if (!pp || !name || !conf)
		return PATH_IS_ERROR;

	if (conf->find_multipaths <= FIND_MULTIPATHS_UNDEF ||
	    conf->find_multipaths >= __FIND_MULTIPATHS_LAST)
		return PATH_IS_ERROR;

	if (safe_sprintf(pp->dev, "%s", name))
		return PATH_IS_ERROR;

	if (sysfs_is_multipathed(pp, true)) {
		if (pp->wwid[0] == '\0')
			return PATH_IS_ERROR;
		return PATH_IS_VALID_NO_CHECK;
	}

	/*
	 * "multipath -u" may be run before the daemon is started. In this
	 * case, systemd might own the socket but might delay multipathd
	 * startup until some other unit (udev settle!)  has finished
	 * starting. With many LUNs, the listen backlog may be exceeded, which
	 * would cause connect() to block. This causes udev workers calling
	 * "multipath -u" to hang, and thus creates a deadlock, until "udev
	 * settle" times out.  To avoid this, call connect() in non-blocking
	 * mode here, and take EAGAIN as indication for a filled-up systemd
	 * backlog.
	 */

	if (check_multipathd) {
		fd = __mpath_connect(1);
		if (fd < 0) {
			if (errno != EAGAIN && !systemd_service_enabled(name)) {
				condlog(3, "multipathd not running or enabled");
				return PATH_IS_NOT_VALID;
			}
		} else
			mpath_disconnect(fd);
	}

	pp->udev = udev_device_new_from_subsystem_sysname(udev, "block", name);
	if (!pp->udev)
		return PATH_IS_ERROR;

	r = pathinfo(pp, conf, DI_SYSFS | DI_WWID | DI_BLACKLIST);
	if (r == PATHINFO_SKIPPED)
		return PATH_IS_NOT_VALID;
	else if (r)
		return PATH_IS_ERROR;

	if (pp->wwid[0] == '\0')
		return PATH_IS_NOT_VALID;

	if (pp->udev && pp->uid_attribute &&
	    filter_property(conf, pp->udev, 3, pp->uid_attribute) > 0)
		return PATH_IS_NOT_VALID;

	r = is_failed_wwid(pp->wwid);
	if (r != WWID_IS_NOT_FAILED) {
		if (r == WWID_IS_FAILED)
			return PATH_IS_NOT_VALID;
		return PATH_IS_ERROR;
	}

	if (conf->find_multipaths == FIND_MULTIPATHS_GREEDY)
		return PATH_IS_VALID;

	if (check_wwids_file(pp->wwid, 0) == 0)
		return PATH_IS_VALID_NO_CHECK;

	if (dm_map_present_by_uuid(pp->wwid) == 1)
		return PATH_IS_VALID;

	/* all these act like FIND_MULTIPATHS_STRICT for finding if a
	 * path is valid */
	if (conf->find_multipaths != FIND_MULTIPATHS_SMART)
		return PATH_IS_NOT_VALID;

	return PATH_IS_MAYBE_VALID;
}
