#include <stdio.h>
#include <string.h>

#include "vector.h"
#include "structs.h"
#include "print.h"

#define MAX_LEN 64

void
get_path_layout (struct path_layout * pl, vector pathvec)
{
	int i;
	int hbtl_len, dev_len, dev_t_len;
	char buff[MAX_LEN];
	struct path * pp;

	/* reset max col lengths */
	pl->hbtl_len = 0;
	pl->dev_len = 0;
	pl->dev_t_len = 0;

	vector_foreach_slot (pathvec, pp, i) {
		hbtl_len = snprintf(buff, MAX_LEN, "%i:%i:%i:%i",
					pp->sg_id.host_no,
					pp->sg_id.channel,
					pp->sg_id.scsi_id,
					pp->sg_id.lun);
		dev_len = strlen(pp->dev);
		dev_t_len = strlen(pp->dev_t);

		pl->hbtl_len = (hbtl_len > pl->hbtl_len) ?
				hbtl_len : pl->hbtl_len;
		pl->dev_len = (dev_len > pl->dev_len) ?
				dev_len : pl->dev_len;
		pl->dev_t_len = (dev_t_len > pl->dev_t_len) ?
				dev_t_len : pl->dev_t_len;
	}
	return;
}

void
get_map_layout (struct map_layout * ml, vector mpvec)
{
	int i;
	char buff[MAX_LEN];
	int mapname_len, mapdev_len;
	struct multipath * mpp;

	/* reset max col lengths */
	ml->mapname_len = 0;
	ml->mapdev_len = 0;

	vector_foreach_slot (mpvec, mpp, i) {
		mapname_len = (mpp->alias) ?
				strlen(mpp->alias) : strlen(mpp->wwid);

		ml->mapname_len = (mapname_len > ml->mapname_len) ?
				mapname_len : ml->mapname_len;

		mapdev_len = snprintf(buff, MAX_LEN, "dm-%i", mpp->minor);

		ml->mapdev_len = (mapdev_len > ml->mapdev_len) ?
				mapdev_len : ml->mapdev_len;
	}
	return;
}

int
print_path_id (char * line, int len, struct path * pp, struct path_layout * pl)
{
	char * c = line;
	char * s = line;

	if ((pl->hbtl_len + pl->dev_len + pl->dev_t_len + 3) > len)
		return 0;

	if (pp->sg_id.host_no < 0)
		c += sprintf(c, "#:#:#:# ");
	else {
		c += sprintf(c, "%i:%i:%i:%i ",
			pp->sg_id.host_no,
			pp->sg_id.channel,
			pp->sg_id.scsi_id,
			pp->sg_id.lun);
	}

	while ((int)(c - s) < (pl->hbtl_len + 1))
		*c++ = ' ';

	s = c;

	if (pp->dev)
		c += sprintf(c, "%s ", pp->dev);

	while ((int)(c - s) < (pl->dev_len + 1))
		*c++ = ' ';

	s = c;

	if (pp->dev_t)
		c += sprintf(c, "%s ", pp->dev_t);

	while ((int)(c - s) < (pl->dev_t_len + 1))
		*c++ = ' ';

	return (c - line);
}

int
print_map_id (char * line, int len, struct multipath * mpp, struct map_layout * ml)
{
	char * c = line;
	char * s = line;

	if ((ml->mapname_len + ml->mapdev_len + 2) > len)
		return 0;

	if (mpp->alias)
		c += sprintf(c, "%s ", mpp->alias);
	else
		c += sprintf(c, "%s ", mpp->wwid);

	while ((int)(c - s) < (ml->mapname_len + 1))
		*c++ = ' ';

	s = c;

	c += sprintf(c, "dm-%i ", mpp->minor);

	while ((int)(c - s) < (ml->mapdev_len + 1))
		*c++ = ' ';

	return (c - line);
}
