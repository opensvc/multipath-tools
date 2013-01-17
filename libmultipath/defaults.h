#define DEFAULT_UID_ATTRIBUTE	"ID_SERIAL"
#define DEFAULT_UDEVDIR		"/dev"
#define DEFAULT_MULTIPATHDIR	"/" LIB_STRING "/multipath"
#define DEFAULT_SELECTOR	"service-time 0"
#define DEFAULT_ALIAS_PREFIX	"mpath"
#define DEFAULT_FEATURES	"0"
#define DEFAULT_HWHANDLER	"0"
#define DEFAULT_MINIO		1000
#define DEFAULT_MINIO_RQ	1
#define DEFAULT_PGPOLICY       FAILOVER
#define DEFAULT_FAILBACK       -FAILBACK_MANUAL
#define DEFAULT_RR_WEIGHT      RR_WEIGHT_NONE
#define DEFAULT_NO_PATH_RETRY  NO_PATH_RETRY_UNDEF
#define DEFAULT_PGTIMEOUT      -PGTIMEOUT_NONE
#define DEFAULT_USER_FRIENDLY_NAMES    0
#define DEFAULT_VERBOSITY	2
#define DEFAULT_REASSIGN_MAPS	1
#define DEFAULT_FAST_IO_FAIL	5
#define DEFAULT_RETAIN_HWHANDLER RETAIN_HWHANDLER_OFF
#define DEFAULT_DETECT_PRIO DETECT_PRIO_OFF

#define DEFAULT_CHECKINT	5
#define MAX_CHECKINT(a)		(a << 2)

#define MAX_DEV_LOSS_TMO	0x7FFFFFFF
#define DEFAULT_PIDFILE		"/var/run/multipathd.pid"
#define DEFAULT_SOCKET		"/org/kernel/linux/storage/multipathd"
#define DEFAULT_CONFIGFILE	"/etc/multipath.conf"
#define DEFAULT_BINDINGS_FILE	"/etc/multipath/bindings"
#define DEFAULT_WWIDS_FILE	"/etc/multipath/wwids"

char * set_default (char * str);
