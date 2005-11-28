/*
 * Copyright (c) 2005 Christophe Varoqui
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <libdevmapper.h>

#include "vector.h"
#include "structs.h"
#include "structs_vec.h"
#include "print.h"
#include "dmparser.h"
#include "configure.h"
#include "defaults.h"
#include "debug.h"

#include "../libcheckers/path_state.h"

#define MAX(x,y) (x > y) ? x : y
#define TAIL     (line + len - 1 - c)
#define NOPAD    s = c
#define PAD(x)   while ((int)(c - s) < (x) && (c < (line + len - 1))) \
			*c++ = ' '; s = c
#define PRINT(var, size, format, args...)      \
	         fwd = snprintf(var, size, format, ##args); \
		 c += (fwd >= size) ? size : fwd;

/* for column aligned output */
struct path_layout pl;
struct map_layout ml;

/*
 * information printing helpers
 */
static int
snprint_str (char * buff, size_t len, char * str)
{
	return snprintf(buff, len, "%s", str);
}

static int
snprint_int (char * buff, size_t len, int val)
{
	return snprintf(buff, len, "%i", val);
}

static int
snprint_uint (char * buff, size_t len, unsigned int val)
{
	return snprintf(buff, len, "%u", val);
}

static int
snprint_name (char * buff, size_t len, struct multipath * mpp)
{
	if (mpp->alias)
		return snprintf(buff, len, "%s", mpp->alias);
	else
		return snprintf(buff, len, "%s", mpp->wwid);
}

static int
snprint_sysfs (char * buff, size_t len, struct multipath * mpp)
{
	if (mpp->dmi)
		return snprintf(buff, len, "dm-%i", mpp->dmi->minor);

	return 0;
}

static int
snprint_progress (char * buff, size_t len, int cur, int total)
{
	int i = PROGRESS_LEN * cur / total;
	int j = PROGRESS_LEN - i;
	char * c = buff;
	char * end = buff + len;
	
	while (i-- > 0) {
		c += snprintf(c, len, "X");
		if ((len = (end - c)) <= 1) goto out;
	}

	while (j-- > 0) {
		c += snprintf(c, len,  ".");
		if ((len = (end - c)) <= 1) goto out;
	}

	c += snprintf(c, len, " %i/%i", cur, total);

out:
	buff[c - buff + 1] = '\0';
	return (c - buff + 1);
}
	
static int
snprint_failback (char * buff, size_t len, struct multipath * mpp)
{
	if (mpp->pgfailback == -FAILBACK_IMMEDIATE)
		return snprintf(buff, len, "immediate");

	if (!mpp->failback_tick)
		return snprintf(buff, len, "-");
	else
		return snprint_progress(buff, len, mpp->failback_tick,
					mpp->pgfailback);
}

static int
snprint_queueing (char * buff, size_t len, struct multipath * mpp)
{
	if (mpp->no_path_retry == NO_PATH_RETRY_FAIL)
		return snprintf(buff, len, "off");
	else if (mpp->no_path_retry == NO_PATH_RETRY_QUEUE)
		return snprintf(buff, len, "on");
	else if (mpp->no_path_retry == NO_PATH_RETRY_UNDEF)
		return snprintf(buff, len, "-");
	else if (mpp->no_path_retry > 0) {
		if (mpp->retry_tick)
			return snprintf(buff, len, "%i sec",
					mpp->retry_tick);
		else
			return snprintf(buff, len, "%i chk",
					mpp->no_path_retry);
	}
	return 0;
}

static int
snprint_nb_paths (char * buff, size_t len, struct multipath * mpp)
{
	return snprint_int(buff, len, mpp->nr_active);
}

static int
snprint_dm_map_state (char * buff, size_t len, struct multipath * mpp)
{
	if (mpp->dmi && mpp->dmi->suspended)
		return snprintf(buff, len, "suspend");
	else
		return snprintf(buff, len, "active");
}

static int
snprint_path_faults (char * buff, size_t len, struct multipath * mpp)
{
	return snprint_uint(buff, len, mpp->stat_path_failures);
}

static int
snprint_switch_grp (char * buff, size_t len, struct multipath * mpp)
{
	return snprint_uint(buff, len, mpp->stat_switchgroup);
}

static int
snprint_map_loads (char * buff, size_t len, struct multipath * mpp)
{
	return snprint_uint(buff, len, mpp->stat_map_loads);
}

static int
snprint_total_q_time (char * buff, size_t len, struct multipath * mpp)
{
	return snprint_uint(buff, len, mpp->stat_total_queueing_time);
}

static int
snprint_q_timeouts (char * buff, size_t len, struct multipath * mpp)
{
	return snprint_uint(buff, len, mpp->stat_queueing_timeouts);
}

static int
snprint_uuid (char * buff, size_t len, struct path * pp)
{
	return snprint_str(buff, len, pp->wwid);
}

static int
snprint_hcil (char * buff, size_t len, struct path * pp)
{
	if (pp->sg_id.host_no < 0)
		return snprintf(buff, len, "#:#:#:#");

	return snprintf(buff, len, "%i:%i:%i:%i",
			pp->sg_id.host_no,
			pp->sg_id.channel,
			pp->sg_id.scsi_id,
			pp->sg_id.lun);
}

static int
snprint_dev (char * buff, size_t len, struct path * pp)
{
	if (!strlen(pp->dev))
		return snprintf(buff, len, "-");
	else
		return snprint_str(buff, len, pp->dev);
}

static int
snprint_dev_t (char * buff, size_t len, struct path * pp)
{
	if (!strlen(pp->dev))
		return snprintf(buff, len, "#:#");
	else
		return snprint_str(buff, len, pp->dev_t);
}

static int
snprint_chk_state (char * buff, size_t len, struct path * pp)
{
	switch (pp->state) {
	case PATH_UP:
		return snprintf(buff, len, "[ready]");
	case PATH_DOWN:
		return snprintf(buff, len, "[faulty]");
	case PATH_SHAKY:
		return snprintf(buff, len, "[shaky]");
	case PATH_GHOST:
		return snprintf(buff, len, "[ghost]");
	default:
		return snprintf(buff, len, "[undef]");
	}
}

static int
snprint_dm_path_state (char * buff, size_t len, struct path * pp)
{
	switch (pp->dmstate) {
	case PSTATE_ACTIVE:
		return snprintf(buff, len, "[active]");
	case PSTATE_FAILED:
		return snprintf(buff, len, "[failed]");
	default:
		return snprintf(buff, len, "[undef]");
	}
}

static int
snprint_vpr (char * buff, size_t len, struct path * pp)
{
	return snprintf(buff, len, "%s/%s/%s",
		        pp->vendor_id, pp->product_id, pp->rev);
}

static int
snprint_next_check (char * buff, size_t len, struct path * pp)
{
	if (!pp->mpp)
		return snprintf(buff, len, "[orphan]");

	return snprint_progress(buff, len, pp->tick, pp->checkint);
}

static int
snprint_pri (char * buff, size_t len, struct path * pp)
{
	return snprint_int(buff, len, pp->priority);
}

struct multipath_data mpd[] = {
	{'w', "name",          0, snprint_name},
	{'d', "sysfs",         0, snprint_sysfs},
	{'F', "failback",      0, snprint_failback},
	{'Q', "queueing",      0, snprint_queueing},
	{'n', "paths",         0, snprint_nb_paths},
	{'t', "dm-st",         0, snprint_dm_map_state},
	{'0', "path_faults",   0, snprint_path_faults},
	{'1', "switch_grp",    0, snprint_switch_grp},
	{'2', "map_loads",     0, snprint_map_loads},
	{'3', "total_q_time",  0, snprint_total_q_time},
	{'4', "q_timeouts",    0, snprint_q_timeouts},
	{0, NULL, 0 , NULL}
};

struct path_data pd[] = {
	{'w', "uuid",          0, snprint_uuid},
	{'i', "hcil",          0, snprint_hcil},
	{'d', "dev",           0, snprint_dev},
	{'D', "dev_t",         0, snprint_dev_t},
	{'t', "dm-st",         0, snprint_dm_path_state},
	{'T', "chk_st",        0, snprint_chk_state},
	{'s', "vend/prod/rev", 0, snprint_vpr},
	{'C', "next_check",    0, snprint_next_check},
	{'p', "pri",           0, snprint_pri},
	{0, NULL, 0 , NULL}
};

void
get_path_layout (vector pathvec)
{
	int i, j;
	char buff[MAX_FIELD_LEN];
	struct path * pp;

	for (j = 0; pd[j].header; j++) {
		pd[j].width = strlen(pd[j].header);

		vector_foreach_slot (pathvec, pp, i) {
			pd[j].snprint(buff, MAX_FIELD_LEN, pp);
			pd[j].width = MAX(pd[j].width, strlen(buff));
		}
	}
}

void
get_multipath_layout (vector mpvec)
{
	int i, j;
	char buff[MAX_FIELD_LEN];
	struct multipath * mpp;

	for (j = 0; mpd[j].header; j++) {
		mpd[j].width = strlen(mpd[j].header);

		vector_foreach_slot (mpvec, mpp, i) {
			mpd[j].snprint(buff, MAX_FIELD_LEN, mpp);
			mpd[j].width = MAX(mpd[j].width, strlen(buff));
		}
	}
}

static struct multipath_data *
mpd_lookup(char wildcard)
{
	int i;

	for (i = 0; mpd[i].header; i++)
		if (mpd[i].wildcard == wildcard)
			return &mpd[i];

	return NULL;
}

static struct path_data *
pd_lookup(char wildcard)
{
	int i;

	for (i = 0; pd[i].header; i++)
		if (pd[i].wildcard == wildcard)
			return &pd[i];

	return NULL;
}

int
snprint_multipath_header (char * line, int len, char * format)
{
	char * c = line;   /* line cursor */
	char * s = line;   /* for padding */
	char * f = format; /* format string cursor */
	int fwd;
	struct multipath_data * data;

	do {
		if (!TAIL)
			break;

		if (*f != '%') {
			*c++ = *f;
			NOPAD;
			continue;
		}
		f++;
		
		if (!(data = mpd_lookup(*f)))
			break; /* unknown wildcard */
		
		PRINT(c, TAIL, data->header);
		PAD(data->width);
	} while (*f++);

	line[c - line - 1] = '\n';
	line[c - line] = '\0';

	return (c - line);
}

int
snprint_multipath (char * line, int len, char * format,
	     struct multipath * mpp)
{
	char * c = line;   /* line cursor */
	char * s = line;   /* for padding */
	char * f = format; /* format string cursor */
	int fwd;
	struct multipath_data * data;
	char buff[MAX_FIELD_LEN];

	do {
		if (!TAIL)
			break;

		if (*f != '%') {
			*c++ = *f;
			NOPAD;
			continue;
		}
		f++;
		
		if (!(data = mpd_lookup(*f)))
			break;
		
		data->snprint(buff, MAX_FIELD_LEN, mpp);
		PRINT(c, TAIL, buff);
		PAD(data->width);
	} while (*f++);

	line[c - line - 1] = '\n';
	line[c - line] = '\0';

	return (c - line);
}

int
snprint_path_header (char * line, int len, char * format)
{
	char * c = line;   /* line cursor */
	char * s = line;   /* for padding */
	char * f = format; /* format string cursor */
	int fwd;
	struct path_data * data;

	do {
		if (!TAIL)
			break;

		if (*f != '%') {
			*c++ = *f;
			NOPAD;
			continue;
		}
		f++;
		
		if (!(data = pd_lookup(*f)))
			break; /* unknown wildcard */
		
		PRINT(c, TAIL, data->header);
		PAD(data->width);
	} while (*f++);

	line[c - line - 1] = '\n';
	line[c - line] = '\0';

	return (c - line);
}

int
snprint_path (char * line, int len, char * format,
	     struct path * pp)
{
	char * c = line;   /* line cursor */
	char * s = line;   /* for padding */
	char * f = format; /* format string cursor */
	int fwd;
	struct path_data * data;
	char buff[MAX_FIELD_LEN];

	do {
		if (!TAIL)
			break;

		if (*f != '%') {
			*c++ = *f;
			NOPAD;
			continue;
		}
		f++;
		
		if (!(data = pd_lookup(*f)))
			break;
		
		data->snprint(buff, MAX_FIELD_LEN, pp);
		PRINT(c, TAIL, buff);
		PAD(data->width);
	} while (*f++);

	line[c - line - 1] = '\n';
	line[c - line] = '\0';

	return (c - line);
}

extern void
print_mp (struct multipath * mpp, int verbosity)
{
	int j, i;
	struct path * pp = NULL;
	struct pathgroup * pgp = NULL;

	if (mpp->action == ACT_NOTHING || !verbosity || !mpp->size)
		return;

	if (verbosity > 1) {
		switch (mpp->action) {
		case ACT_RELOAD:
			printf("%s: ", ACT_RELOAD_STR);
			break;

		case ACT_CREATE:
			printf("%s: ", ACT_CREATE_STR);
			break;

		case ACT_SWITCHPG:
			printf("%s: ", ACT_SWITCHPG_STR);
			break;

		default:
			break;
		}
	}

	if (mpp->alias)
		printf("%s", mpp->alias);

	if (verbosity == 1) {
		printf("\n");
		return;
	}
	if (strncmp(mpp->alias, mpp->wwid, WWID_SIZE))
		printf(" (%s)", mpp->wwid);

	printf("\n");

	if (mpp->size < (1 << 11))
		printf("[size=%llu kB]", mpp->size >> 1);
	else if (mpp->size < (1 << 21))
		printf("[size=%llu MB]", mpp->size >> 11);
	else if (mpp->size < (1 << 31))
		printf("[size=%llu GB]", mpp->size >> 21);
	else
		printf("[size=%llu TB]", mpp->size >> 31);

	if (mpp->features)
		printf("[features=\"%s\"]", mpp->features);

	if (mpp->hwhandler)
		printf("[hwhandler=\"%s\"]", mpp->hwhandler);

	fprintf(stdout, "\n");

	if (!mpp->pg)
		return;

	vector_foreach_slot (mpp->pg, pgp, j) {
		printf("\\_ ");

		if (mpp->selector) {
			printf("%s ", mpp->selector);
#if 0
			/* align to path status info */
			for (i = pl.hbtl_len + pl.dev_len + pl.dev_t_len + 4;
			     i > strlen(mpp->selector); i--)
				printf(" ");
#endif
		}
		if (pgp->priority)
			printf("[prio=%i]", pgp->priority);

		switch (pgp->status) {
		case PGSTATE_ENABLED:
			printf("[enabled]");
			break;
		case PGSTATE_DISABLED:
			printf("[disabled]");
			break;
		case PGSTATE_ACTIVE:
			printf("[active]");
			break;
		default:
			break;
		}
		printf("\n");

		vector_foreach_slot (pgp->paths, pp, i)
			print_path(pp, PRINT_PATH_INDENT);
	}
	printf("\n");
}

extern void
print_path (struct path * pp, char * style)
{
	char line[MAX_LINE_LEN];

	snprint_path(&line[0], MAX_LINE_LEN, style, pp);
	printf("%s", line);
}

extern void
print_multipath (struct multipath * mpp, char * style)
{
	char line[MAX_LINE_LEN];

	snprint_multipath(&line[0], MAX_LINE_LEN, style, mpp);
	printf("%s", line);
}

extern void
print_map (struct multipath * mpp)
{
	if (mpp->size && mpp->params)
		printf("0 %llu %s %s\n",
			 mpp->size, DEFAULT_TARGET, mpp->params);
	return;
}

extern void
print_all_paths (vector pathvec, int banner)
{
	int i;
	struct path * pp;
	char line[MAX_LINE_LEN];

	if (!VECTOR_SIZE(pathvec)) {
		if (banner)
			fprintf(stdout, "===== no paths =====\n");
		return;
	}
	
	if (banner)
		fprintf(stdout, "===== paths list =====\n");

	get_path_layout(pathvec);
	snprint_path_header(line, MAX_LINE_LEN, PRINT_PATH_LONG);
	fprintf(stdout, "%s", line);

	vector_foreach_slot (pathvec, pp, i)
		print_path(pp, PRINT_PATH_LONG);
}

