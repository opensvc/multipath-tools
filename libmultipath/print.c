#include <stdio.h>
#include <string.h>

#include "vector.h"
#include "structs.h"
#include "print.h"

#define MAX_HBTL_LEN 64

void
get_path_layout (struct path_layout * pl, vector pathvec)
{
	int i;
	int hbtl_len, dev_len, dev_t_len;
	char buff[MAX_HBTL_LEN];
	struct path * pp;

	/* reset max col lenghts */
	pl->hbtl_len = 0;
	pl->dev_len = 0;
	pl->dev_t_len = 0;

	vector_foreach_slot (pathvec, pp, i) {
		hbtl_len = snprintf(buff, MAX_HBTL_LEN, "%i:%i:%i:%i",
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

	if (pp->dev) {
		c += sprintf(c, "%s ", pp->dev);

		while ((int)(c - s) < (pl->dev_len + 1))
			*c++ = ' ';
	}

	s = c;

	if (pp->dev_t) {
		c += sprintf(c, "%s ", pp->dev_t);

		while ((int)(c - s) < (pl->dev_t_len + 1))
			*c++ = ' ';
	}

	s = c;

	return (c - line);
}
