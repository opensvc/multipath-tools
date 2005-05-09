#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libdevmapper.h>
#include <signal.h>
#include <wait.h>
#include <sched.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/mman.h>

/*
 * libsysfs
 */
#include <sysfs/libsysfs.h>
#include <sysfs/dlist.h>

/*
 * libcheckers
 */
#include <checkers.h>
#include <path_state.h>

/*
 * libmultipath
 */
#include <parser.h>
#include <vector.h>
#include <memory.h>
#include <config.h>
#include <callout.h>
#include <util.h>
#include <blacklist.h>
#include <hwtable.h>
#include <defaults.h>
#include <structs.h>
#include <dmparser.h>
#include <devmapper.h>
#include <dict.h>
#include <discovery.h>
#include <debug.h>
#include <propsel.h>
#include <uevent.h>

#include "main.h"
#include "copy.h"
#include "clone_platform.h"
#include "pidfile.h"

#define FILE_NAME_SIZE 256
#define CMDSIZE 160

#define CALLOUT_DIR "/var/cache/multipathd"

#define LOG_MSG(a,b) \
	if (strlen(a)) { \
		log_safe(LOG_WARNING, "%s: %s", b, a); \
		memset(a, 0, MAX_CHECKER_MSG_SIZE); \
	}

#ifdef LCKDBG
#define lock(a) \
	fprintf(stderr, "%s:%s(%i) lock %p\n", __FILE__, __FUNCTION__, __LINE__, a); \
	pthread_mutex_lock(a)
#define unlock(a) \
	fprintf(stderr, "%s:%s(%i) unlock %p\n", __FILE__, __FUNCTION__, __LINE__, a); \
	pthread_mutex_unlock(a)
#else
#define lock(a) pthread_mutex_lock(a)
#define unlock(a) pthread_mutex_unlock(a)
#endif

/*
 * global vars
 */
int pending_event = 0;
pthread_mutex_t *event_lock;
pthread_cond_t *event;

/*
 * structs
 */
struct paths {
	pthread_mutex_t *lock;
	vector pathvec;
};

struct event_thread {
	pthread_t *thread;
	pthread_mutex_t *waiter_lock;
	int lease;
	int event_nr;
	char mapname[WWID_SIZE];
	struct paths *allpaths;
};

int 
uev_trigger (struct uevent * uev, void * trigger_data)
{
	int r = 0;
	int i;
	char devname[32];
	struct path * pp;
	struct paths * allpaths;

	allpaths = (struct paths *)trigger_data;

	if (strncmp(uev->devpath, "/block", 6))
		goto out;

	basename(uev->devpath, devname);

	if (blacklist(conf->blist, devname))
		goto out;

	lock(allpaths->lock);
	pp = find_path_by_dev(allpaths->pathvec, devname);

	r = 1;

	if (pp && !strncmp(uev->action, "remove", 6)) {
		condlog(2, "remove %s path checker", devname);
		i = find_slot(allpaths->pathvec, (void *)pp);
		vector_del_slot(allpaths->pathvec, i);
		free_path(pp);
	}
	if (!pp && !strncmp(uev->action, "add", 3)) {
		condlog(2, "add %s path checker", devname);
		store_pathinfo(allpaths->pathvec, conf->hwtable,
			       devname, DI_SYSFS | DI_WWID);
	}
	unlock(allpaths->lock);

	r = 0;
out:
	FREE(uev);
	return r;
}

static void *
ueventloop (void * ap)
{
	uevent_listen(&uev_trigger, ap);

	return NULL;
}

static void
strvec_free (vector vec)
{
	int i;
	char * str;

	vector_foreach_slot (vec, str, i)
		if (str)
			FREE(str);

	vector_free(vec);
}

static int
exit_daemon (int status)
{
	if (status != 0)
		fprintf(stderr, "bad exit status. see daemon.log\n");

	log_safe(LOG_INFO, "umount ramfs");
	umount(CALLOUT_DIR);

	log_safe(LOG_INFO, "unlink pidfile");
	unlink(DEFAULT_PIDFILE);

	log_safe(LOG_NOTICE, "--------shut down-------");
	log_thread_stop();
	exit(status);
}

static void
set_paths_owner (struct paths * allpaths, struct multipath * mpp)
{
	int i;
	struct path * pp;

	lock(allpaths->lock);

	vector_foreach_slot (allpaths->pathvec, pp, i)
		if (!strncmp(mpp->wwid, pp->wwid, WWID_SIZE))
			pp->mpp = mpp;

	unlock(allpaths->lock);
}

static int
get_dm_mpvec (vector mpvec, struct paths * allpaths)
{
	int i;
	struct multipath * mpp;
	char * wwid;

	if (dm_get_maps(mpvec, "multipath"))
		return 1;

	vector_foreach_slot (mpvec, mpp, i) {
		wwid = get_mpe_wwid(mpp->alias);

		if (wwid) {
			strncpy(mpp->wwid, wwid, WWID_SIZE);
			wwid = NULL;
		} else
			strncpy(mpp->wwid, mpp->alias, WWID_SIZE);

		set_paths_owner(allpaths, mpp);
        }
        return 0;
}

static int
path_discovery_locked (struct paths *allpaths)
{
	lock(allpaths->lock);
	path_discovery(allpaths->pathvec, conf, DI_SYSFS | DI_WWID);
	unlock(allpaths->lock);

	return 0;
}

static int
mark_failed_path (struct paths *allpaths, char *mapname)
{
	struct multipath *mpp;
	struct pathgroup  *pgp;
	struct path *pp;
	struct path *app;
	int i, j;
	int r = 1;

	if (!dm_map_present(mapname))
		return 0;

	mpp = alloc_multipath();

	if (!mpp)
		return 1;

	if (dm_get_map(mapname, &mpp->size, (char *)mpp->params))
		goto out;

	if (dm_get_status(mapname, mpp->status))
		goto out;
	
	lock(allpaths->lock);
	r = disassemble_map(allpaths->pathvec, mpp->params, mpp);
	unlock(allpaths->lock);
	
	if (r)
		goto out;

	r = disassemble_status(mpp->status, mpp);

	if (r)
		goto out;

	r = 0; /* can't fail from here on */
	lock(allpaths->lock);

	vector_foreach_slot (mpp->pg, pgp, i) {
		vector_foreach_slot (pgp->paths, pp, j) {
			if (pp->dmstate != PSTATE_FAILED)
				continue;

			app = find_path_by_devt(allpaths->pathvec, pp->dev_t);

			if (app && app->state != PATH_DOWN) {
				log_safe(LOG_NOTICE, "mark %s as failed",
					pp->dev_t);
				app->state = PATH_DOWN;
			}
		}
	}
	unlock(allpaths->lock);
out:
	free_multipath(mpp, KEEP_PATHS);

	return r;
}

static void *
waiteventloop (struct event_thread * waiter, char * cmd)
{
	struct dm_task *dmt;
	int event_nr;

	if (!waiter->event_nr)
		waiter->event_nr = dm_geteventnr(waiter->mapname);

	if (!(dmt = dm_task_create(DM_DEVICE_WAITEVENT)))
		return 0;

	if (!dm_task_set_name(dmt, waiter->mapname))
		goto out;

	if (waiter->event_nr && !dm_task_set_event_nr(dmt, waiter->event_nr))
		goto out;

	dm_task_no_open_count(dmt);

	dm_task_run(dmt);

	waiter->event_nr++;

	/*
	 * upon event ...
	 */
	while (1) {
		log_safe(LOG_DEBUG, "%s", cmd);
		log_safe(LOG_NOTICE, "devmap event (%i) on %s",
				waiter->event_nr, waiter->mapname);

		mark_failed_path(waiter->allpaths, waiter->mapname);

		event_nr = dm_geteventnr(waiter->mapname);

		if (waiter->event_nr == event_nr)
			break;

		waiter->event_nr = event_nr;
	}

out:
	dm_task_destroy(dmt);

	/*
	 * tell waiterloop we have an event
	 */
	lock(event_lock);
	pending_event++;
	pthread_cond_signal(event);
	unlock(event_lock);
	
	return NULL;
}

static void *
waitevent (void * et)
{
	struct event_thread *waiter;
	char cmd[CMDSIZE];

	mlockall(MCL_CURRENT | MCL_FUTURE);

	waiter = (struct event_thread *)et;
	lock(waiter->waiter_lock);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	if (safe_snprintf(cmd, CMDSIZE, "%s %s",
			  conf->multipath, waiter->mapname)) {
		log_safe(LOG_ERR, "command too long, abord reconfigure");
		goto out;
	}
	while (1)
		waiteventloop(waiter, cmd);

out:
	/*
	 * release waiter_lock so that waiterloop knows we are gone
	 */
	unlock(waiter->waiter_lock);
	pthread_exit(waiter->thread);

	return NULL;
}

static void *
alloc_waiter (void)
{

	struct event_thread * wp;

	wp = MALLOC(sizeof(struct event_thread));

	if (!wp)
		return NULL;

	wp->thread = MALLOC(sizeof(pthread_t));

	if (!wp->thread)
		goto out;
		
	wp->waiter_lock = (pthread_mutex_t *)MALLOC(sizeof(pthread_mutex_t));

	if (!wp->waiter_lock)
		goto out1;

	pthread_mutex_init(wp->waiter_lock, NULL);
	return wp;

out1:
	free(wp->thread);
out:
	free(wp);
	return NULL;
}

static void
free_waiter (struct event_thread * wp)
{
	pthread_mutex_destroy(wp->waiter_lock);
	free(wp->thread);
	free(wp);
}

static void
fail_path (struct path * pp)
{
	if (!pp->mpp)
		return;

	log_safe(LOG_NOTICE, "checker failed path %s in map %s",
		 pp->dev_t, pp->mpp->alias);

	dm_fail_path(pp->mpp->alias, pp->dev_t);
}

static void *
waiterloop (void *ap)
{
	struct paths *allpaths;
	struct multipath *mpp;
	vector mpvec = NULL;
	vector waiters;
	struct event_thread *wp;
	pthread_attr_t attr;
	int r = 1;
	int i, j;

	mlockall(MCL_CURRENT | MCL_FUTURE);
	log_safe(LOG_NOTICE, "start DM events thread");

	if (sysfs_get_mnt_path(sysfs_path, FILE_NAME_SIZE)) {
		log_safe(LOG_ERR, "can not find sysfs mount point");
		return NULL;
	}

	/*
	 * inits
	 */
	allpaths = (struct paths *)ap;
	waiters = vector_alloc();

	if (!waiters)
		return NULL;

	if (pthread_attr_init(&attr))
		return NULL;

	pthread_attr_setstacksize(&attr, 32 * 1024);

	/*
	 * update paths list
	 */
	while(path_discovery_locked(allpaths)) {
		if (r) {
			/* log only once */
			log_safe(LOG_ERR, "can't update path list ... retry");
			r = 0;
		}
		sleep(5);
	}
	log_safe(LOG_INFO, "path list updated");


	while (1) {
		/*
		 * revoke the leases
		 */
		vector_foreach_slot(waiters, wp, i)
			wp->lease = 0;

		/*
		 * update multipaths list
		 */
		if (mpvec)
			free_multipathvec(mpvec, KEEP_PATHS);

		while (1) {
			/*
			 * we're not allowed to fail here
			 */
			mpvec = vector_alloc();

			if (mpvec && !get_dm_mpvec(mpvec, allpaths))
				break;

			if (!r) {
				/* log only once */
				log_safe(LOG_ERR, "can't get mpvec ... retry");
				r = 1;
			}
			sleep(5);
		}
		log_safe(LOG_INFO, "multipath list updated");

		/*
		 * start waiters on all mpvec
		 */
		log_safe(LOG_INFO, "start up event loops");

		vector_foreach_slot (mpvec, mpp, i) {
			/*
			 * find out if devmap already has
			 * a running waiter thread
			 */
			vector_foreach_slot (waiters, wp, j)
				if (!strcmp(wp->mapname, mpp->alias))
					break;
					
			/*
			 * no event_thread struct : init it
			 */
			if (j == VECTOR_SIZE(waiters)) {
				wp = alloc_waiter();

				if (!wp)
					continue;

				strncpy(wp->mapname, mpp->alias, WWID_SIZE);
				wp->allpaths = allpaths;

				if (!vector_alloc_slot(waiters)) {
					free_waiter(wp);
					continue;
				}
				vector_set_slot(waiters, wp);
			}
			
			/*
			 * event_thread struct found
			 */
			else if (j < VECTOR_SIZE(waiters)) {
				r = pthread_mutex_trylock(wp->waiter_lock);

				/*
				 * thread already running : next devmap
				 */
				if (r) {
					log_safe(LOG_DEBUG,
						 "event checker running : %s",
						 wp->mapname);

					/*
					 * renew the lease
					 */
					wp->lease = 1;
					continue;
				}
				pthread_mutex_unlock(wp->waiter_lock);
			}
			
			if (pthread_create(wp->thread, &attr, waitevent, wp)) {
				log_safe(LOG_ERR,
					 "cannot create event checker : %s",
					 wp->mapname);
				free_waiter(wp);
				vector_del_slot(waiters, j);
				continue;
			}
			wp->lease = 1;
			log_safe(LOG_NOTICE, "event checker started : %s",
					wp->mapname);
		}
		vector_foreach_slot (waiters, wp, i) {
			if (wp->lease == 0) {
				log_safe(LOG_NOTICE, "reap event checker : %s",
					wp->mapname);

				pthread_cancel(*wp->thread);
				free_waiter(wp);
				vector_del_slot(waiters, i);
				i--;
			}
		}
		/*
		 * wait event condition
		 */
		lock(event_lock);

		if (pending_event > 0)
			pending_event--;

		log_safe(LOG_INFO, "%i pending event(s)", pending_event);
		if(pending_event == 0)
			pthread_cond_wait(event, event_lock);

		unlock(event_lock);
	}
	return NULL;
}

static void *
checkerloop (void *ap)
{
	struct paths *allpaths;
	struct path *pp;
	int i;
	int newstate;
	char buff[1];
	char cmd[CMDSIZE];
	char checker_msg[MAX_CHECKER_MSG_SIZE];

	mlockall(MCL_CURRENT | MCL_FUTURE);

	memset(checker_msg, 0, MAX_CHECKER_MSG_SIZE);
	allpaths = (struct paths *)ap;

	log_safe(LOG_NOTICE, "path checkers start up");

	while (1) {
		lock(allpaths->lock);
		log_safe(LOG_DEBUG, "checking paths");

		vector_foreach_slot (allpaths->pathvec, pp, i) {
			if (!pp->checkfn) {
				pathinfo(pp, conf->hwtable, DI_SYSFS);
				select_checkfn(pp);
			}

			if (!pp->checkfn) {
				log_safe(LOG_ERR, "%s: checkfn is void",
					 pp->dev);
				continue;
			}
			newstate = pp->checkfn(pp->fd, checker_msg,
					       &pp->checker_context);
			
			if (newstate != pp->state) {
				pp->state = newstate;
				LOG_MSG(checker_msg, pp->dev_t);

				/*
				 * proactively fail path in the DM
				 */
				if (newstate == PATH_DOWN ||
				    newstate == PATH_SHAKY) {
					fail_path(pp);
					continue;
				}

				/*
				 * reconfigure map now
				 */
				if (safe_snprintf(cmd, CMDSIZE, "%s %s",
						  conf->multipath, pp->dev_t)) {
					log_safe(LOG_ERR, "command too long,"
							" abord reconfigure");
				} else {
					log_safe(LOG_DEBUG, "%s", cmd);
					log_safe(LOG_INFO,
						"reconfigure %s multipath",
						pp->dev_t);
					execute_program(cmd, buff, 1);
				}

				/*
				 * tell waiterloop we have an event
				 */
				lock (event_lock);
				pending_event++;
				pthread_cond_signal(event);
				unlock (event_lock);
			}
			pp->state = newstate;
		}
		unlock(allpaths->lock);
		sleep(conf->checkint);
	}
	return NULL;
}

static struct paths *
init_paths (void)
{
	struct paths *allpaths;

	allpaths = MALLOC(sizeof(struct paths));

	if (!allpaths)
		return NULL;

	allpaths->lock = 
		(pthread_mutex_t *)MALLOC(sizeof(pthread_mutex_t));

	if (!allpaths->lock)
		goto out;

	allpaths->pathvec = vector_alloc();

	if (!allpaths->pathvec)
		goto out1;
		
	pthread_mutex_init (allpaths->lock, NULL);

	return (allpaths);
out1:
	FREE(allpaths->lock);
out:
	FREE(allpaths);
	return NULL;
}

static int
init_event (void)
{
	event = (pthread_cond_t *)MALLOC(sizeof (pthread_cond_t));

	if (!event)
		return 1;

	pthread_cond_init (event, NULL);
	event_lock = (pthread_mutex_t *) MALLOC (sizeof (pthread_mutex_t));

	if (!event_lock)
		goto out;

	pthread_mutex_init (event_lock, NULL);
	
	return 0;
out:
	FREE(event);
	return 1;
}
/*
 * this logic is all about keeping callouts working in case of
 * system disk outage (think system over SAN)
 * this needs the clone syscall, so don't bother if not present
 * (Debian Woody)
 */
#ifdef CLONE_NEWNS
static int
prepare_namespace(void)
{
	mode_t mode = S_IRWXU;
	struct stat *buf;
	char ramfs_args[64];
	int i;
	int fd;
	char * bin;
	size_t size = 10;
	struct stat statbuf;
	
	buf = MALLOC(sizeof(struct stat));

	/*
	 * create a temp mount point for ramfs
	 */
	if (stat(CALLOUT_DIR, buf) < 0) {
		if (mkdir(CALLOUT_DIR, mode) < 0) {
			log_safe(LOG_ERR, "cannot create " CALLOUT_DIR);
			return -1;
		}
		log_safe(LOG_DEBUG, "created " CALLOUT_DIR);
	}

	/*
	 * compute the optimal ramdisk size
	 */
	vector_foreach_slot (conf->binvec, bin,i) {
		if ((fd = open(bin, O_RDONLY)) < 0) {
			log_safe(LOG_ERR, "cannot open %s", bin);
			return -1;
		}
		if (fstat(fd, &statbuf) < 0) {
			log_safe(LOG_ERR, "cannot stat %s", bin);
			return -1;
		}
		size += statbuf.st_size;
		close(fd);
	}
	log_safe(LOG_INFO, "ramfs maxsize is %u", (unsigned int) size);
	
	/*
	 * mount the ramfs
	 */
	if (safe_sprintf(ramfs_args, "maxsize=%u", (unsigned int) size)) {
		fprintf(stderr, "ramfs_args too small\n");
		return -1;
	}
	if (mount(NULL, CALLOUT_DIR, "ramfs", MS_SYNCHRONOUS, ramfs_args) < 0) {
		log_safe(LOG_ERR, "cannot mount ramfs on " CALLOUT_DIR);
		return -1;
	}
	log_safe(LOG_DEBUG, "mount ramfs on " CALLOUT_DIR);

	/*
	 * populate the ramfs with callout binaries
	 */
	vector_foreach_slot (conf->binvec, bin,i) {
		if (copytodir(bin, CALLOUT_DIR) < 0) {
			log_safe(LOG_ERR, "cannot copy %s in ramfs", bin);
			exit_daemon(1);
		}
		log_safe(LOG_DEBUG, "cp %s in ramfs", bin);
	}
	strvec_free(conf->binvec);

	/*
	 * bind the ramfs to :
	 * /sbin : default home of multipath ...
	 * /bin  : default home of scsi_id ...
	 * /tmp  : home of scsi_id temp files
	 */
	if (mount(CALLOUT_DIR, "/sbin", NULL, MS_BIND, NULL) < 0) {
		log_safe(LOG_ERR, "cannot bind ramfs on /sbin");
		return -1;
	}
	log_safe(LOG_DEBUG, "bind ramfs on /sbin");
	if (mount(CALLOUT_DIR, "/bin", NULL, MS_BIND, NULL) < 0) {
		log_safe(LOG_ERR, "cannot bind ramfs on /bin");
		return -1;
	}
	log_safe(LOG_DEBUG, "bind ramfs on /bin");
	if (mount(CALLOUT_DIR, "/tmp", NULL, MS_BIND, NULL) < 0) {
		log_safe(LOG_ERR, "cannot bind ramfs on /tmp");
		return -1;
	}
	log_safe(LOG_DEBUG, "bind ramfs on /tmp");

	return 0;
}
#endif

static void *
signal_set(int signo, void (*func) (int))
{
	int r;
	struct sigaction sig;
	struct sigaction osig;

	sig.sa_handler = func;
	sigemptyset(&sig.sa_mask);
	sig.sa_flags = 0;

	r = sigaction(signo, &sig, &osig);

	if (r < 0)
		return (SIG_ERR);
	else
		return (osig.sa_handler);
}

static void
sighup (int sig)
{
	log_safe(LOG_NOTICE, "SIGHUP received");

#ifdef _DEBUG_
	dbg_free_final(NULL);
#endif
}

static void
sigend (int sig)
{
	exit_daemon(0);
}

static void
signal_init(void)
{
	signal_set(SIGHUP, sighup);
	signal_set(SIGINT, sigend);
	signal_set(SIGTERM, sigend);
	signal_set(SIGKILL, sigend);
}

static void
setscheduler (void)
{
        int res;
	static struct sched_param sched_param = {
		sched_priority: 99
	};

        res = sched_setscheduler (0, SCHED_RR, &sched_param);

        if (res == -1)
                log_safe(LOG_WARNING, "Could not set SCHED_RR at priority 99");
	return;
}

static void
set_oom_adj (int val)
{
	FILE *fp;

	fp = fopen("/proc/self/oom_adj", "w");

	if (!fp)
		return;

	fprintf(fp, "%i", val);
	fclose(fp);
}
	
static int
child (void * param)
{
	pthread_t wait_thr, check_thr, uevent_thr;
	pthread_attr_t attr;
	struct paths * allpaths;

	mlockall(MCL_CURRENT | MCL_FUTURE);

	log_thread_start();
	log_safe(LOG_NOTICE, "--------start up--------");
	log_safe(LOG_NOTICE, "read " DEFAULT_CONFIGFILE);

	if (load_config(DEFAULT_CONFIGFILE))
		exit(1);

	setlogmask(LOG_UPTO(conf->verbosity + 3));

	/*
	 * fill the voids left in the config file
	 */
	if (conf->binvec == NULL) {
		conf->binvec = vector_alloc();
		push_callout("/sbin/scsi_id");
	}
	if (conf->multipath == NULL) {
		conf->multipath = MULTIPATH;
		push_callout(conf->multipath);
	}

	if (pidfile_create(DEFAULT_PIDFILE, getpid())) {
		log_thread_stop();
		exit(1);
	}
	signal_init();
	setscheduler();
	set_oom_adj(-17);
	allpaths = init_paths();

	if (!allpaths || init_event())
		exit(1);

	conf->checkint = CHECKINT;

#ifdef CLONE_NEWNS
	if (prepare_namespace() < 0) {
		log_safe(LOG_ERR, "cannot prepare namespace");
		exit_daemon(1);
	}
#endif

	/*
	 * start threads
	 */
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 64 * 1024);
	
	pthread_create(&wait_thr, &attr, waiterloop, allpaths);
	pthread_create(&check_thr, &attr, checkerloop, allpaths);
	pthread_create(&uevent_thr, &attr, ueventloop, allpaths);
	pthread_join(wait_thr, NULL);
	pthread_join(check_thr, NULL);
	pthread_join(uevent_thr, NULL);

	return 0;
}

int
main (int argc, char *argv[])
{
	extern char *optarg;
	extern int optind;
	int arg;
	int err;
	void * child_stack;
	
	if (getuid() != 0) {
		fprintf(stderr, "need to be root\n");
		exit(1);
	}

	/* make sure we don't lock any path */
	chdir("/");
	umask(umask(077) | 022);

	child_stack = (void *)malloc(CHILD_STACK_SIZE);

	if (!child_stack)
		exit(1);

	conf = alloc_config();

	if (!conf)
		exit(1);

	while ((arg = getopt(argc, argv, ":qdlFSi:v:p:")) != EOF ) {
	switch(arg) {
		case 'v':
			if (sizeof(optarg) > sizeof(char *) ||
			    !isdigit(optarg[0]))
				exit(1);

			conf->verbosity = atoi(optarg);
			break;
		default:
			;
		}
	}

#ifdef CLONE_NEWNS	/* recent systems have clone() */

#    if defined(__hppa__) || defined(__powerpc64__)
	err = clone(child, child_stack, CLONE_NEWNS, NULL);
#    elif defined(__ia64__)
	err = clone2(child, child_stack,
		     CHILD_STACK_SIZE, CLONE_NEWNS, NULL,
		     NULL, NULL, NULL);
#    else
	err = clone(child, child_stack + CHILD_STACK_SIZE, CLONE_NEWNS, NULL);
#    endif
	if (err < 0)
		exit (1);

	exit(0);
#else			/* older system fallback to fork() */
	err = fork();
	
	if (err < 0)
		exit (1);

	return (child(child_stack));
#endif

}
