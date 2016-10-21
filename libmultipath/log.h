#ifndef LOG_H
#define LOG_H

#define DEFAULT_AREA_SIZE 16384
#define MAX_MSG_SIZE 256

#ifndef LOGLEVEL
#define LOGLEVEL 5
#endif

#if LOGDBG
#define logdbg(file, fmt, args...) fprintf(file, fmt, ##args)
#else
#define logdbg(file, fmt, args...) do {} while (0)
#endif

struct logmsg {
	short int prio;
	void * next;
	char str[0];
};

struct logarea {
	int empty;
	void * head;
	void * tail;
	void * start;
	void * end;
	char * buff;
};

extern struct logarea* la;

int log_init (char * progname, int size);
void log_close (void);
void log_reset (char * progname);
int log_enqueue (int prio, const char * fmt, va_list ap);
int log_dequeue (void *);
void log_syslog (void *);
void dump_logmsg (void *);
void free_logarea (void);

#endif /* LOG_H */
