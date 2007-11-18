/*
 * Copyright (c) 2005 Christophe Varoqui
 */
#include <stdio.h>
#include <string.h>
#include <libdevmapper.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <dirent.h>

#include <checkers.h>

#include "vector.h"
#include "structs.h"
#include "structs_vec.h"
#include "print.h"
#include "dmparser.h"
#include "config.h"
#include "configure.h"
#include "pgpolicies.h"
#include "defaults.h"
#include "parser.h"
#include "blacklist.h"
#include "switchgroup.h"

#define MAX(x,y) (x > y) ? x : y
#define TAIL     (line + len - 1 - c)
#define NOPAD    s = c
#define PAD(x)   while ((int)(c - s) < (x) && (c < (line + len - 1))) \
			*c++ = ' '; s = c
#define PRINT(var, size, format, args...)      \
	         fwd = snprintf(var, size, format, ##args); \
		 c += (fwd >= size) ? size : fwd;

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
snprint_size (char * buff, size_t len, unsigned long long size)
{
	float s = (float)(size >> 1); /* start with KB */
	char fmt[6] = {};
	char units[] = {'K','M','G','T','P'};
	char *u = units;
	
	while (s >= 1024 && *u != 'P') {
		s = s / 1024;
		u++;
	}
	if (s < 10)
		snprintf(fmt, 6, "%%.1f%c", *u);
	else
		snprintf(fmt, 6, "%%.0f%c", *u);

	return snprintf(buff, len, fmt, s);
}

/*
 * multipath info printing functions
 */
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
snprint_multipath_size (char * buff, size_t len, struct multipath * mpp)
{
	return snprint_size(buff, len, mpp->size);
}

static int
snprint_features (char * buff, size_t len, struct multipath * mpp)
{
	return snprint_str(buff, len, mpp->features);
}

static int
snprint_hwhandler (char * buff, size_t len, struct multipath * mpp)
{
	return snprint_str(buff, len, mpp->hwhandler);
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
snprint_multipath_uuid (char * buff, size_t len, struct multipath * mpp)
{
	return snprint_str(buff, len, mpp->wwid);
}

static int
snprint_action (char * buff, size_t len, struct multipath * mpp)
{
	switch (mpp->action) {
	case ACT_REJECT:
		return snprint_str(buff, len, ACT_REJECT_STR);
	case ACT_RENAME:
		return snprint_str(buff, len, ACT_RENAME_STR);
	case ACT_RELOAD:
		return snprint_str(buff, len, ACT_RELOAD_STR);
	case ACT_CREATE:
		return snprint_str(buff, len, ACT_CREATE_STR);
	case ACT_SWITCHPG:
		return snprint_str(buff, len, ACT_SWITCHPG_STR);
	default:
		return 0;
	}
}

/*
 * path info printing functions
 */
static int
snprint_path_uuid (char * buff, size_t len, struct path * pp)
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
	return snprintf(buff, len, "%s,%s",
		        pp->vendor_id, pp->product_id);
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

static int
snprint_pg_selector (char * buff, size_t len, struct pathgroup * pgp)
{
	return snprint_str(buff, len, pgp->selector);
}

static int
snprint_pg_pri (char * buff, size_t len, struct pathgroup * pgp)
{
	/*
	 * path group priority is not updated for every path prio change,
	 * but only on switch group code path.
	 *
	 * Printing is another reason to update.
	 */
	path_group_prio_update(pgp);
	return snprint_int(buff, len, pgp->priority);
}

static int
snprint_pg_state (char * buff, size_t len, struct pathgroup * pgp)
{
	switch (pgp->status) {
	case PGSTATE_ENABLED:
		return snprintf(buff, len, "[enabled]");
	case PGSTATE_DISABLED:
		return snprintf(buff, len, "[disabled]");
	case PGSTATE_ACTIVE:
		return snprintf(buff, len, "[active]");
	default:
		return snprintf(buff, len, "[undef]");
	}
}

static int
snprint_path_size (char * buff, size_t len, struct path * pp)
{
	return snprint_size(buff, len, pp->size);
}

struct multipath_data mpd[] = {
	{'n', "name",          0, snprint_name},
	{'w', "uuid",          0, snprint_multipath_uuid},
	{'d', "sysfs",         0, snprint_sysfs},
	{'F', "failback",      0, snprint_failback},
	{'Q', "queueing",      0, snprint_queueing},
	{'N', "paths",         0, snprint_nb_paths},
	{'t', "dm-st",         0, snprint_dm_map_state},
	{'S', "size",          0, snprint_multipath_size},
	{'f', "features",      0, snprint_features},
	{'h', "hwhandler",     0, snprint_hwhandler},
	{'A', "action",        0, snprint_action},
	{'0', "path_faults",   0, snprint_path_faults},
	{'1', "switch_grp",    0, snprint_switch_grp},
	{'2', "map_loads",     0, snprint_map_loads},
	{'3', "total_q_time",  0, snprint_total_q_time},
	{'4', "q_timeouts",    0, snprint_q_timeouts},
	{0, NULL, 0 , NULL}
};

struct path_data pd[] = {
	{'w', "uuid",          0, snprint_path_uuid},
	{'i', "hcil",          0, snprint_hcil},
	{'d', "dev",           0, snprint_dev},
	{'D', "dev_t",         0, snprint_dev_t},
	{'t', "dm_st",         0, snprint_dm_path_state},
	{'T', "chk_st",        0, snprint_chk_state},
	{'s', "vend/prod/rev", 0, snprint_vpr},
	{'C', "next_check",    0, snprint_next_check},
	{'p', "pri",           0, snprint_pri},
	{'S', "size",          0, snprint_path_size},
	{0, NULL, 0 , NULL}
};

struct pathgroup_data pgd[] = {
	{'s', "selector",      0, snprint_pg_selector},
	{'p', "pri",           0, snprint_pg_pri},
	{'t', "dm_st",         0, snprint_pg_state},
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

static struct pathgroup_data *
pgd_lookup(char wildcard)
{
	int i;

	for (i = 0; pgd[i].header; i++)
		if (pgd[i].wildcard == wildcard)
			return &pgd[i];

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
	char buff[MAX_FIELD_LEN] = {};

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
		buff[0] = '\0';
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

int
snprint_pathgroup (char * line, int len, char * format,
		   struct pathgroup * pgp)
{
	char * c = line;   /* line cursor */
	char * s = line;   /* for padding */
	char * f = format; /* format string cursor */
	int fwd;
	struct pathgroup_data * data;
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
		
		if (!(data = pgd_lookup(*f)))
			break;
		
		data->snprint(buff, MAX_FIELD_LEN, pgp);
		PRINT(c, TAIL, buff);
		PAD(data->width);
	} while (*f++);

	line[c - line - 1] = '\n';
	line[c - line] = '\0';

	return (c - line);
}

extern void
print_multipath_topology (struct multipath * mpp, int verbosity)
{
	char buff[MAX_LINE_LEN * MAX_LINES] = {};

	snprint_multipath_topology(&buff[0], MAX_LINE_LEN * MAX_LINES,
				   mpp, verbosity);
	printf("%s", buff);
}

extern int
snprint_multipath_topology (char * buff, int len, struct multipath * mpp,
			    int verbosity)
{
	int j, i, fwd = 0;
	struct path * pp = NULL;
	struct pathgroup * pgp = NULL;
	char style[64];
	char * c = style;

	if (verbosity <= 0)
		return fwd;

	if (verbosity == 1)
		return snprint_multipath(buff, len, "%n", mpp);

	if (verbosity > 1 &&
	    mpp->action != ACT_NOTHING &&
	    mpp->action != ACT_UNDEF)
			c += sprintf(c, "%%A: ");

	c += sprintf(c, "%%n");
	
	if (strncmp(mpp->alias, mpp->wwid, WWID_SIZE))
		c += sprintf(c, " (%%w) ");

	c += sprintf(c, "%%d ");
	c += snprint_vpr(c, 24, first_path(mpp));

	fwd += snprint_multipath(buff + fwd, len - fwd, style, mpp);
	if (fwd > len)
		return len;
	fwd += snprint_multipath(buff + fwd, len - fwd,
				 "[size=%S][features=%f][hwhandler=%h]", mpp);
	if (fwd > len)
		return len;

	if (!mpp->pg)
		return fwd;

	vector_foreach_slot (mpp->pg, pgp, j) {
		pgp->selector = mpp->selector; /* hack */
		fwd += snprint_pathgroup(buff + fwd, len - fwd,
					 PRINT_PG_INDENT, pgp);
		if (fwd > len)
			return len;

		vector_foreach_slot (pgp->paths, pp, i) {
			fwd += snprint_path(buff + fwd, len - fwd,
					    PRINT_PATH_INDENT, pp);
			if (fwd > len)
				return len;
		}
	}
	return fwd;
}

static int
snprint_hwentry (char * buff, int len, struct hwentry * hwe)
{
	int i;
	int fwd = 0;
	struct keyword * kw;
	struct keyword * rootkw;

	rootkw = find_keyword(NULL, "devices");

	if (!rootkw || !rootkw->sub)
		return 0;

	rootkw = find_keyword(rootkw->sub, "device");

	if (!rootkw)
		return 0;

	fwd += snprintf(buff + fwd, len - fwd, "\tdevice {\n");
	if (fwd > len)
		return len;
	iterate_sub_keywords(rootkw, kw, i) {
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t\t%k %v\n",
				kw, hwe);
		if (fwd > len)
			return len;
	}
	fwd += snprintf(buff + fwd, len - fwd, "\t}\n");
	if (fwd > len)
		return len;
	return fwd;
}

extern int
snprint_hwtable (char * buff, int len, vector hwtable)
{
	int fwd = 0;
	int i;
	struct hwentry * hwe;
	struct keyword * rootkw;

	rootkw = find_keyword(NULL, "devices");
	if (!rootkw)
		return 0;

	fwd += snprintf(buff + fwd, len - fwd, "devices {\n");
	if (fwd > len)
		return len;
	vector_foreach_slot (hwtable, hwe, i) {
		fwd += snprint_hwentry(buff + fwd, len - fwd, hwe);
		if (fwd > len)
			return len;
	}
	fwd += snprintf(buff + fwd, len - fwd, "}\n");
	if (fwd > len)
		return len;
	return fwd;
}

static int
snprint_mpentry (char * buff, int len, struct mpentry * mpe)
{
	int i;
	int fwd = 0;
	struct keyword * kw;
	struct keyword * rootkw;

	rootkw = find_keyword(NULL, "multipath");
	if (!rootkw)
		return 0;

	fwd += snprintf(buff + fwd, len - fwd, "\tmultipath {\n");
	if (fwd > len)
		return len;
	iterate_sub_keywords(rootkw, kw, i) {
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t\t%k %v\n",
				kw, mpe);
		if (fwd > len)
			return len;
	}
	fwd += snprintf(buff + fwd, len - fwd, "\t}\n");
	if (fwd > len)
		return len;
	return fwd;
}

extern int
snprint_mptable (char * buff, int len, vector mptable)
{
	int fwd = 0;
	int i;
	struct mpentry * mpe;
	struct keyword * rootkw;

	rootkw = find_keyword(NULL, "multipaths");
	if (!rootkw)
		return 0;

	fwd += snprintf(buff + fwd, len - fwd, "multipaths {\n");
	if (fwd > len)
		return len;
	vector_foreach_slot (mptable, mpe, i) {
		fwd += snprint_mpentry(buff + fwd, len - fwd, mpe);
		if (fwd > len)
			return len;
	}
	fwd += snprintf(buff + fwd, len - fwd, "}\n");
	if (fwd > len)
		return len;
	return fwd;
}

extern int
snprint_defaults (char * buff, int len)
{
	int fwd = 0;
	int i;
	struct keyword *rootkw;
	struct keyword *kw;

	rootkw = find_keyword(NULL, "defaults");
	if (!rootkw)
		return 0;

	fwd += snprintf(buff + fwd, len - fwd, "defaults {\n");
	if (fwd > len)
		return len;

	iterate_sub_keywords(rootkw, kw, i) {
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t%k %v\n",
				kw, NULL);
		if (fwd > len)
			return len;
	}
	fwd += snprintf(buff + fwd, len - fwd, "}\n");
	if (fwd > len)
		return len;
	return fwd;
	
}

static int
snprint_blacklist_group (char *buff, int len, int *fwd, vector *vec)
{
	int threshold = MAX_LINE_LEN;
	struct blentry * ble;
	int pos;
	int i;

	pos = *fwd;
	if (!VECTOR_SIZE(*vec)) {
		if ((len - pos - threshold) <= 0)
			return 0;
		pos += snprintf(buff + pos, len - pos, "        <empty>\n");
	} else vector_foreach_slot (*vec, ble, i) {
		if ((len - pos - threshold) <= 0)
			return 0;
		if (ble->origin == ORIGIN_CONFIG)
			pos += snprintf(buff + pos, len - pos, "        (config file rule) ");
		else if (ble->origin == ORIGIN_DEFAULT)
			pos += snprintf(buff + pos, len - pos, "        (default rule)     ");
		pos += snprintf(buff + pos, len - pos, "%s\n", ble->str);
	}

	*fwd = pos;
	return pos;
}

static int
snprint_blacklist_devgroup (char *buff, int len, int *fwd, vector *vec)
{
	int threshold = MAX_LINE_LEN;
	struct blentry_device * bled;
	int pos;
	int i;

	pos = *fwd;
	if (!VECTOR_SIZE(*vec)) {
		if ((len - pos - threshold) <= 0)
			return 0;
		pos += snprintf(buff + pos, len - pos, "        <empty>\n");
	} else vector_foreach_slot (*vec, bled, i) {
		if ((len - pos - threshold) <= 0)
			return 0;
		if (bled->origin == ORIGIN_CONFIG)
			pos += snprintf(buff + pos, len - pos, "        (config file rule) ");
		else if (bled->origin == ORIGIN_DEFAULT)
			pos += snprintf(buff + pos, len - pos, "        (default rule)     ");
		pos += snprintf(buff + pos, len - pos, "%s:%s\n", bled->vendor, bled->product);
	}

	*fwd = pos;
	return pos;
}

extern int
snprint_blacklist_report (char * buff, int len)
{
	int threshold = MAX_LINE_LEN;
	int fwd = 0;

	if ((len - fwd - threshold) <= 0)
		return len;
	fwd += snprintf(buff + fwd, len - fwd, "device node rules:\n"
			                       "- blacklist:\n");
	if (!snprint_blacklist_group(buff, len, &fwd, &conf->blist_devnode))
		return len;

	if ((len - fwd - threshold) <= 0)
		return len;
	fwd += snprintf(buff + fwd, len - fwd, "- exceptions:\n");
	if (snprint_blacklist_group(buff, len, &fwd, &conf->elist_devnode) == 0)
		return len;

	if ((len - fwd - threshold) <= 0)
		return len;
	fwd += snprintf(buff + fwd, len - fwd, "wwid rules:\n"
			                       "- blacklist:\n");
	if (snprint_blacklist_group(buff, len, &fwd, &conf->blist_wwid) == 0)
		return len;

	if ((len - fwd - threshold) <= 0)
		return len;
	fwd += snprintf(buff + fwd, len - fwd, "- exceptions:\n");
	if (snprint_blacklist_group(buff, len, &fwd, &conf->elist_wwid) == 0)
		return len;

	if ((len - fwd - threshold) <= 0)
		return len;
	fwd += snprintf(buff + fwd, len - fwd, "device rules:\n"
			                       "- blacklist:\n");
	if (snprint_blacklist_devgroup(buff, len, &fwd, &conf->blist_device) == 0)
		return len;

	if ((len - fwd - threshold) <= 0)
		return len;
	fwd += snprintf(buff + fwd, len - fwd, "- exceptions:\n");
	if (snprint_blacklist_devgroup(buff, len, &fwd, &conf->elist_device) == 0)
		return len;

	if (fwd > len)
		return len;
	return fwd;
}

extern int
snprint_blacklist (char * buff, int len)
{
	int i;
	struct blentry * ble;
	struct blentry_device * bled;
	int fwd = 0;
	struct keyword *rootkw;
	struct keyword *kw;

	rootkw = find_keyword(NULL, "blacklist");
	if (!rootkw)
		return 0;

	fwd += snprintf(buff + fwd, len - fwd, "blacklist {\n");
	if (fwd > len)
		return len;

	vector_foreach_slot (conf->blist_devnode, ble, i) {
		kw = find_keyword(rootkw->sub, "devnode");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t%k %v\n",
				       kw, ble);
		if (fwd > len)
			return len;
	}
	vector_foreach_slot (conf->blist_wwid, ble, i) {
		kw = find_keyword(rootkw->sub, "wwid");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t%k %v\n",
				       kw, ble);
		if (fwd > len)
			return len;
	}
	rootkw = find_keyword(rootkw->sub, "device");
	if (!rootkw)
		return 0;

	vector_foreach_slot (conf->blist_device, bled, i) {
		fwd += snprintf(buff + fwd, len - fwd, "\tdevice {\n");
		if (fwd > len)
			return len;
		kw = find_keyword(rootkw->sub, "vendor");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t\t%k %v\n",
				       kw, bled);
		if (fwd > len)
			return len;
		kw = find_keyword(rootkw->sub, "product");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t\t%k %v\n",
				       kw, bled);
		if (fwd > len)
			return len;
		fwd += snprintf(buff + fwd, len - fwd, "\t}\n");
		if (fwd > len)
			return len;
	}
	fwd += snprintf(buff + fwd, len - fwd, "}\n");
	if (fwd > len)
		return len;
	return fwd;
}

extern int
snprint_blacklist_except (char * buff, int len)
{
	int i;
	struct blentry * ele;
	struct blentry_device * eled;
	int fwd = 0;
	struct keyword *rootkw;
	struct keyword *kw;

	rootkw = find_keyword(NULL, "blacklist_exceptions");
	if (!rootkw)
		return 0;

	fwd += snprintf(buff + fwd, len - fwd, "blacklist_exceptions {\n");
	if (fwd > len)
		return len;

	vector_foreach_slot (conf->elist_devnode, ele, i) {
		kw = find_keyword(rootkw->sub, "devnode");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t%k %v\n",
				       kw, ele);
		if (fwd > len)
			return len;
	}
	vector_foreach_slot (conf->elist_wwid, ele, i) {
		kw = find_keyword(rootkw->sub, "wwid");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t%k %v\n",
				       kw, ele);
		if (fwd > len)
			return len;
	}
	rootkw = find_keyword(rootkw->sub, "device");
	if (!rootkw)
		return 0;

	vector_foreach_slot (conf->elist_device, eled, i) {
		fwd += snprintf(buff + fwd, len - fwd, "\tdevice {\n");
		if (fwd > len)
			return len;
		kw = find_keyword(rootkw->sub, "vendor");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t\t%k %v\n",
				       kw, eled);
		if (fwd > len)
			return len;
		kw = find_keyword(rootkw->sub, "product");
		if (!kw)
			return 0;
		fwd += snprint_keyword(buff + fwd, len - fwd, "\t\t%k %v\n",
				       kw, eled);
		if (fwd > len)
			return len;
		fwd += snprintf(buff + fwd, len - fwd, "\t}\n");
		if (fwd > len)
			return len;
	}
	fwd += snprintf(buff + fwd, len - fwd, "}\n");
	if (fwd > len)
		return len;
	return fwd;
}

extern int
snprint_devices (char * buff, int len, struct vectors *vecs)
{
	DIR *blkdir;
	struct dirent *blkdev;
	struct stat statbuf;
	char devpath[PATH_MAX];
	char *devptr;
	int threshold = MAX_LINE_LEN;
	int fwd = 0;
	int r;

	struct path * pp;

	if (!(blkdir = opendir("/sys/block")))
		return 1;

	if ((len - fwd - threshold) <= 0)
		return len;
	fwd += snprintf(buff + fwd, len - fwd, "available block devices:\n");

	strcpy(devpath,"/sys/block/");
	while ((blkdev = readdir(blkdir)) != NULL) {
		if ((strcmp(blkdev->d_name,".") == 0) ||
		    (strcmp(blkdev->d_name,"..") == 0))
			continue;

		devptr = devpath + 11;
		*devptr = '\0';
		strncat(devptr, blkdev->d_name, PATH_MAX-12);
		if (stat(devpath, &statbuf) < 0)
			continue;

		if (S_ISDIR(statbuf.st_mode) == 0)
			continue;

		if ((len - fwd - threshold)  <= 0)
			return len;

		fwd += snprintf(buff + fwd, len - fwd, "    %s", devptr);
		pp = find_path_by_dev(vecs->pathvec, devptr);
		if (!pp) {
			r = filter_devnode(conf->blist_devnode,
					   conf->elist_devnode, devptr);
			if (r > 0)
				fwd += snprintf(buff + fwd, len - fwd,
						" devnode blacklisted, unmonitored");
			else if (r < 0)
				fwd += snprintf(buff + fwd, len - fwd,
						" devnode whitelisted, unmonitored");
		} else
			fwd += snprintf(buff + fwd, len - fwd,
					" devnode whitelisted, monitored");
		fwd += snprintf(buff + fwd, len - fwd, "\n");
	}
	closedir(blkdir);

	if (fwd > len)
		return len;
	return fwd;
}

extern int
snprint_config (char * buff, int len)
{
	return 0;
}

/*
 * stdout printing helpers
 */
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
print_pathgroup (struct pathgroup * pgp, char * style)
{
	char line[MAX_LINE_LEN];

	snprint_pathgroup(&line[0], MAX_LINE_LEN, style, pgp);
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

