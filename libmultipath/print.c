#include <stdio.h>
#include <string.h>

#include "vector.h"
#include "structs.h"
#include "print.h"
#include "dmparser.h"

#include "../libcheckers/path_state.h"

#define MAX_FIELD_LEN 64

#define MAX(x,y) (x > y) ? x : y;

void
get_path_layout (struct path_layout * pl, vector pathvec)
{
	int i;
	int hbtl_len, dev_len, dev_t_len;
	char buff[MAX_FIELD_LEN];
	struct path * pp;

	/* reset max col lengths */
	pl->hbtl_len = 0;
	pl->dev_len = 0;
	pl->dev_t_len = 0;

	vector_foreach_slot (pathvec, pp, i) {
		hbtl_len = snprintf(buff, MAX_FIELD_LEN, "%i:%i:%i:%i",
					pp->sg_id.host_no,
					pp->sg_id.channel,
					pp->sg_id.scsi_id,
					pp->sg_id.lun);
		dev_len = strlen(pp->dev);
		dev_t_len = strlen(pp->dev_t);

		pl->hbtl_len = MAX(hbtl_len, pl->hbtl_len);
		pl->dev_len = MAX(dev_len, pl->dev_len);
		pl->dev_t_len = MAX(dev_t_len, pl->dev_t_len);
	}
	return;
}

void
get_map_layout (struct map_layout * ml, vector mpvec)
{
	int i;
	char buff[MAX_FIELD_LEN];
	int mapname_len, mapdev_len;
	struct multipath * mpp;

	/* reset max col lengths */
	ml->mapname_len = 0;
	ml->mapdev_len = 0;

	vector_foreach_slot (mpvec, mpp, i) {
		mapname_len = (mpp->alias) ?
				strlen(mpp->alias) : strlen(mpp->wwid);
		ml->mapname_len = MAX(mapname_len, ml->mapname_len);

		mapdev_len = snprintf(buff, MAX_FIELD_LEN,
			       	      "dm-%i", mpp->minor);
		ml->mapdev_len = MAX(mapdev_len, ml->mapdev_len);
	}
	return;
}

#define TAIL   (line + len - 1 - c)
#define PAD(x) while ((int)(c - s) < (x)) *c++ = ' '; s = c
#define NOPAD  s = c
#define PRINT(var, size, format, args...)      \
	        fwd = snprintf(var, size, format, ##args); \
		c += (fwd >= size) ? size : fwd;

int
snprint_map (char * line, int len, char * format,
	     struct multipath * mpp, struct map_layout * ml)
{
	char * c = line;   /* line cursor */
	char * s = line;   /* for padding */
	char * f = format; /* format string cursor */
	int i, j;
	int fwd;

	do {
		if (!TAIL)
			break;

		if (*f != '%') {
			*c++ = *f;
			NOPAD;
			continue;
		}
		f++;
		switch (*f) {
		case 'w':	
			if (mpp->alias) {
				PRINT(c, TAIL, "%s", mpp->alias);
			} else {
				PRINT(c, TAIL, "%s", mpp->wwid);
			}
			PAD(ml->mapname_len);
			break;
		case 'd':
			PRINT(c, TAIL, "dm-%i", mpp->minor);
			PAD(ml->mapdev_len);
			break;
		case 'F':
			if (!mpp->failback_tick) {
				PRINT(c, TAIL, "[no scheduled failback]");
				NOPAD;
				break;
			}
			i = mpp->failback_tick;
			j = mpp->pgfailback - mpp->failback_tick;

			while (i-- > 0) {
				PRINT(c, TAIL, "X");
			}
			while (j-- > 0) {
				PRINT(c, TAIL, ".");
			}
			PRINT(c, TAIL, " %i/%i",
				     mpp->failback_tick, mpp->pgfailback);
			NOPAD;
			break;
		default:
			break;
		}
	} while (*f++);

	line[c - line - 1] = '\n';
	line[c - line] = '\0';

	return (c - line);
}

int
snprint_path (char * line, int len, char * format, struct path * pp,
	    struct path_layout * pl)
{
	char * c = line;   /* line cursor */
	char * s = line;   /* for padding */
	char * f = format; /* format string cursor */
	int i, j;
	int fwd;

	do {
		if (!TAIL)
			break;

		if (*f != '%') {
			*c++ = *f;
			NOPAD;
			continue;
		}
		f++;
		switch (*f) {
		case 'w':	
			PRINT(c, TAIL, "%s ", pp->wwid);
			NOPAD;
			break;
		case 'i':
			if (pp->sg_id.host_no < 0) {
				PRINT(c, TAIL, "#:#:#:# ");
			} else {
				PRINT(c, TAIL, "%i:%i:%i:%i",
					pp->sg_id.host_no,
					pp->sg_id.channel,
					pp->sg_id.scsi_id,
					pp->sg_id.lun);
			}
			PAD(pl->hbtl_len);
			break;
		case 'd':
			PRINT(c, TAIL, "%s", pp->dev);
			PAD(pl->dev_len);
			break;
		case 'D':
			PRINT(c, TAIL, "%s", pp->dev_t);
			PAD(pl->dev_t_len);
			break;
		case 'T':
			switch (pp->state) {
			case PATH_UP:
				PRINT(c, TAIL, "[ready]");
				break;
			case PATH_DOWN:
				PRINT(c, TAIL, "[faulty]");
				break;
			case PATH_SHAKY:
				PRINT(c, TAIL, "[shaky]");
				break;
			case PATH_GHOST:
				PRINT(c, TAIL, "[ghost]");
				break;
			default:
				break;
			}
			NOPAD;
			break;
		case 't':
			switch (pp->dmstate) {
			case PSTATE_ACTIVE:
				PRINT(c, TAIL, "[active]");
				break;
			case PSTATE_FAILED:
				PRINT(c, TAIL, "[failed]");
				break;
			default:
				break;
			}
			NOPAD;
			break;
		case 'c':
			if (pp->claimed && pp->dmstate == PSTATE_UNDEF) {
				PRINT(c, TAIL, "[claimed]");
			}
			NOPAD;
			break;
		case 's':
			PRINT(c, TAIL, "%s/%s/%s",
				      pp->vendor_id, pp->product_id, pp->rev);
			NOPAD;
			break;
		case 'C':
			if (!pp->mpp) {
				PRINT(c, TAIL, "[orphan]");
				NOPAD;
				break;
			}
			i = pp->tick;
			j = pp->checkint - pp->tick;

			while (i-- > 0) {
				PRINT(c, TAIL, "X");
			}
			while (j-- > 0) {
				PRINT(c, TAIL, ".");
			}
			PRINT(c, TAIL, " %i/%i",
				      pp->tick, pp->checkint);
			NOPAD;
			break;
		case 'p':
			if (pp->priority) {
				PRINT(c, TAIL, "%i", pp->priority);
			}
			NOPAD;
			break;
		default:
			break;
		}
	} while (*f++);

	line[c - line - 1] = '\n';
	line[c - line] = '\0';

	return (c - line);
}

