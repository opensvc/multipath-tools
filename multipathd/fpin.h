#ifndef __FPIN_H__
#define __FPIN_H__
#include "autoconfig.h"

#ifdef FPIN_EVENT_HANDLER
void *fpin_fabric_notification_receiver(void *unused);
void *fpin_els_li_consumer(void *data);
void fpin_clean_marginal_dev_list(__attribute__((unused)) void *arg);
#else
static void *fpin_fabric_notification_receiver(__attribute__((unused))void *unused)
{
	return NULL;
}
static void *fpin_els_li_consumer(__attribute__((unused))void *data)
{
	return NULL;
}
/* fpin_clean_marginal_dev_list() is never called */
#endif

#endif
