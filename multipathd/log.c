#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <syslog.h>

#include "log.h"

#if LOGDBG
static void dump_logarea (void)
{
	struct logmsg * msg;
	
	logdbg(stderr, "\n==== area: start addr = %p, end addr = %p ====\n",
		la->start, la->end);
	logdbg(stderr, "|addr     |next     |prio|msg\n");

	for (msg = (struct logmsg *)la->head; (void *)msg != la->tail;
	     msg = msg->next)
		logdbg(stderr, "|%p |%p |%i   |%s\n", (void *)msg, msg->next,
				msg->prio, (char *)&msg->str);

	logdbg(stderr, "|%p |%p |%i   |%s\n", (void *)msg, msg->next,
			msg->prio, (char *)&msg->str);

	logdbg(stderr, "\n\n");
}
#endif
		
static int logarea_init (int size)
{
	logdbg(stderr,"enter logarea_init\n");
	la = malloc(sizeof(struct logarea));
	
	if (!la)
		return 1;

	if (size < MAX_MSG_SIZE)
		size = DEFAULT_AREA_SIZE;

	la->start = malloc(size);
	memset(la->start, 0, size);

	if (!la->start) {
		free(la);
		return 1;
	}

	la->empty = 1;
	la->end = la->start + size;
	la->head = la->start;
	la->tail = la->start;

	la->buff = malloc(MAX_MSG_SIZE + sizeof(struct logmsg));

	if (!la->buff) {
		free(la->start);
		free(la);
		return 1;
	}
	return 0;

}

int log_init(char *program_name, int size)
{
	logdbg(stderr,"enter log_init\n");
	openlog(program_name, 0, LOG_DAEMON);

	if (logarea_init(size))
		return 1;

	return 0;
}

void free_logarea (void)
{
	free(la->start);
	free(la->buff);
	free(la);
	return;
}

void log_close (void)
{
	free_logarea();
	closelog();

	return;
}

int log_enqueue (int prio, const char * fmt, va_list ap)
{
	int len, fwd;
	char buff[MAX_MSG_SIZE];
	struct logmsg * msg;
	struct logmsg * lastmsg;

	lastmsg = (struct logmsg *)la->tail;

	if (!la->empty) {
		fwd = sizeof(struct logmsg) + 
		      strlen((char *)&lastmsg->str) * sizeof(char) + 1;
		la->tail += fwd;
	}
	vsnprintf(buff, MAX_MSG_SIZE, fmt, ap);
	len = strlen(buff) * sizeof(char) + 1;

	/* not enough space on tail : rewind */
	if (la->head <= la->tail &&
	    (len + sizeof(struct logmsg)) > (la->end - la->tail)) {
		logdbg(stderr, "enqueue: rewind tail to %p\n", la->tail);
		la->tail = la->start;
	}

	/* not enough space on head : drop msg */
	if (la->head > la->tail &&
	    (len + sizeof(struct logmsg)) > (la->head - la->tail)) {
		logdbg(stderr, "enqueue: log area overrun, drop msg\n");

		if (!la->empty)
			la->tail = lastmsg;

		return 1;
	}

	/* ok, we can stage the msg in the area */
	la->empty = 0;
	msg = (struct logmsg *)la->tail;
	msg->prio = prio;
	memcpy((void *)&msg->str, buff, len);
	lastmsg->next = la->tail;
	msg->next = la->head;

	logdbg(stderr, "enqueue: %p, %p, %i, %s\n", (void *)msg, msg->next,
		msg->prio, (char *)&msg->str);

#if LOGDBG
	dump_logarea();
#endif
	return 0;
}

int log_dequeue (void * buff)
{
	struct logmsg * src = (struct logmsg *)la->head;
	struct logmsg * dst = (struct logmsg *)buff;
	struct logmsg * lst = (struct logmsg *)la->tail;

	if (la->empty)
		return 1;
	
	int len = strlen((char *)&src->str) * sizeof(char) +
		  sizeof(struct logmsg) + 1;

	dst->prio = src->prio;
	memcpy(dst, src,  len);

	if (la->tail == la->head)
		la->empty = 1; /* we purge the last logmsg */
	else {
		la->head = src->next;
		lst->next = la->head;
	}
	logdbg(stderr, "dequeue: %p, %p, %i, %s\n",
		(void *)src, src->next, src->prio, (char *)&src->str);

	memset((void *)src, 0,  len);

	return la->empty;
}

/*
 * this one can block under memory pressure
 */
void log_syslog (void * buff)
{
	struct logmsg * msg = (struct logmsg *)buff;

	syslog(msg->prio, "%s", (char *)&msg->str);
}
