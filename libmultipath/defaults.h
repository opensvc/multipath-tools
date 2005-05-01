#define DEFAULT_GETUID		"/sbin/scsi_id -g -u -s /block/%n"
#define DEFAULT_UDEVDIR		"/dev"
#define DEFAULT_SELECTOR	"round-robin 0"
#define DEFAULT_FEATURES	"0"
#define DEFAULT_HWHANDLER	"0"
#define DEFAULT_CHECKER		"readsector0"

#define DEFAULT_TARGET		"multipath"
#define DEFAULT_PIDFILE		"/var/run/multipathd.pid"
#define DEFAULT_RUNFILE		"/var/run/multipath.run"
#define DEFAULT_CONFIGFILE	"/etc/multipath.conf"

char * set_default (char * str);
