#ifndef _PRINT_H
#define _PRINT_H
#include "dm-generic.h"

#define PRINT_PATH_LONG      "%w %i %d %D %p %t %T %s %o"
#define PRINT_PATH_INDENT    "%i %d %D %t %T %o"
#define PRINT_PATH_CHECKER   "%i %d %D %p %t %T %o %C"
#define PRINT_MAP_STATUS     "%n %F %Q %N %t %r"
#define PRINT_MAP_STATS      "%n %0 %1 %2 %3 %4"
#define PRINT_MAP_NAMES      "%n %d %w"
#define PRINT_MAP_PROPS      "size=%S features='%f' hwhandler='%h' wp=%r"
#define PRINT_PG_INDENT      "policy='%s' prio=%p status=%t"

#define PRINT_JSON_MULTIPLIER     5
#define PRINT_JSON_MAJOR_VERSION  0
#define PRINT_JSON_MINOR_VERSION  1
#define PRINT_JSON_START_VERSION  "   \"major_version\": %d,\n" \
				  "   \"minor_version\": %d,\n"
#define PRINT_JSON_START_ELEM     "{\n"
#define PRINT_JSON_START_MAP      "   \"map\":"
#define PRINT_JSON_START_MAPS     "\"maps\": ["
#define PRINT_JSON_START_PATHS    "\"paths\": ["
#define PRINT_JSON_START_GROUPS   "\"path_groups\": ["
#define PRINT_JSON_END_ELEM       "},"
#define PRINT_JSON_END_LAST_ELEM  "}"
#define PRINT_JSON_END_LAST       "}\n"
#define PRINT_JSON_END_ARRAY      "]\n"
#define PRINT_JSON_INDENT    "   "
#define PRINT_JSON_MAP       "{\n" \
			     "      \"name\" : \"%n\",\n" \
			     "      \"uuid\" : \"%w\",\n" \
			     "      \"sysfs\" : \"%d\",\n" \
			     "      \"failback\" : \"%F\",\n" \
			     "      \"queueing\" : \"%Q\",\n" \
			     "      \"paths\" : %N,\n" \
			     "      \"write_prot\" : \"%r\",\n" \
			     "      \"dm_st\" : \"%t\",\n" \
			     "      \"features\" : \"%f\",\n" \
			     "      \"hwhandler\" : \"%h\",\n" \
			     "      \"action\" : \"%A\",\n" \
			     "      \"path_faults\" : %0,\n" \
			     "      \"vend\" : \"%v\",\n" \
			     "      \"prod\" : \"%p\",\n" \
			     "      \"rev\" : \"%e\",\n" \
			     "      \"switch_grp\" : %1,\n" \
			     "      \"map_loads\" : %2,\n" \
			     "      \"total_q_time\" : %3,\n" \
			     "      \"q_timeouts\" : %4,"

#define PRINT_JSON_GROUP     "{\n" \
			     "         \"selector\" : \"%s\",\n" \
			     "         \"pri\" : %p,\n" \
			     "         \"dm_st\" : \"%t\",\n" \
			     "         \"marginal_st\" : \"%M\","

#define PRINT_JSON_GROUP_NUM "         \"group\" : %d,\n"

#define PRINT_JSON_PATH      "{\n" \
			     "            \"dev\" : \"%d\",\n"\
			     "            \"dev_t\" : \"%D\",\n" \
			     "            \"dm_st\" : \"%t\",\n" \
			     "            \"dev_st\" : \"%o\",\n" \
			     "            \"chk_st\" : \"%T\",\n" \
			     "            \"checker\" : \"%c\",\n" \
			     "            \"pri\" : %p,\n" \
			     "            \"host_wwnn\" : \"%N\",\n" \
			     "            \"target_wwnn\" : \"%n\",\n" \
			     "            \"host_wwpn\" : \"%R\",\n" \
			     "            \"target_wwpn\" : \"%r\",\n" \
			     "            \"host_adapter\" : \"%a\",\n" \
			     "            \"marginal_st\" : \"%M\""

#define MAX_LINE_LEN  80
#define MAX_LINES     64
#define MAX_FIELD_LEN 128
#define PROGRESS_LEN  10

struct path_data {
	char wildcard;
	char * header;
	int width;
	int (*snprint)(char * buff, size_t len, const struct path * pp);
};

struct multipath_data {
	char wildcard;
	char * header;
	int width;
	int (*snprint)(char * buff, size_t len, const struct multipath * mpp);
};

struct pathgroup_data {
	char wildcard;
	char * header;
	int width;
	int (*snprint)(char * buff, size_t len, const struct pathgroup * pgp);
};

enum layout_reset {
	LAYOUT_RESET_NOT,
	LAYOUT_RESET_ZERO,
	LAYOUT_RESET_HEADER,
};

void _get_path_layout (const struct _vector *gpvec, enum layout_reset);
void get_path_layout (vector pathvec, int header);
void _get_multipath_layout (const struct _vector *gmvec, enum layout_reset);
void get_multipath_layout (vector mpvec, int header);
int snprint_path_header (char *, int, const char *);
int snprint_multipath_header (char *, int, const char *);
int _snprint_path (const struct gen_path *, char *, int, const char *, int);
#define snprint_path(buf, len, fmt, pp, v) \
	_snprint_path(dm_path_to_gen(pp), buf, len, fmt,  v)
int _snprint_multipath (const struct gen_multipath *, char *, int,
			const char *, int);
#define snprint_multipath(buf, len, fmt, mp, v)				\
	_snprint_multipath(dm_multipath_to_gen(mp), buf, len, fmt,  v)
int _snprint_multipath_topology (const struct gen_multipath *, char *, int,
				 int verbosity);
#define snprint_multipath_topology(buf, len, mpp, v) \
	_snprint_multipath_topology (dm_multipath_to_gen(mpp), buf, len, v)
int snprint_multipath_topology_json (char * buff, int len,
				const struct vectors * vecs);
char *snprint_config(const struct config *conf, int *len,
		     const struct _vector *hwtable,
		     const struct _vector *mpvec);
int snprint_multipath_map_json (char * buff, int len,
				const struct multipath * mpp, int last);
int snprint_blacklist_report (struct config *, char *, int);
int snprint_wildcards (char *, int);
int snprint_status (char *, int, const struct vectors *);
int snprint_devices (struct config *, char *, int, const struct vectors *);
int snprint_path_serial (char *, size_t, const struct path *);
int snprint_host_wwnn (char *, size_t, const struct path *);
int snprint_host_wwpn (char *, size_t, const struct path *);
int snprint_tgt_wwnn (char *, size_t, const struct path *);
int snprint_tgt_wwpn (char *, size_t, const struct path *);
#define PROTOCOL_BUF_SIZE sizeof("scsi:unspec")
int snprint_path_protocol(char *, size_t, const struct path *);

void _print_multipath_topology (const struct gen_multipath * gmp,
				int verbosity);
#define print_multipath_topology(mpp, v) \
	_print_multipath_topology(dm_multipath_to_gen(mpp), v)

void print_all_paths (vector pathvec, int banner);
void print_all_paths_custo (vector pathvec, int banner, char *fmt);

int snprint_path_attr(const struct gen_path* gp,
		      char *buf, int len, char wildcard);
int snprint_pathgroup_attr(const struct gen_pathgroup* gpg,
			   char *buf, int len, char wildcard);
int snprint_multipath_attr(const struct gen_multipath* gm,
			   char *buf, int len, char wildcard);
int snprint_multipath_style(const struct gen_multipath *gmp,
			    char *style, int len, int verbosity);
#endif /* _PRINT_H */
