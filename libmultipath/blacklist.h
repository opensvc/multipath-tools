#ifndef _BLACKLIST_H
#define _BLACKLIST_H

#include "regex.h"

struct blentry {
	char * str;
	regex_t regex;
	int origin;
};

struct blentry_device {
	char * vendor;
	char * product;
	regex_t vendor_reg;
	regex_t product_reg;
	int origin;
};

int setup_default_blist (struct config *);
int alloc_ble_device (vector);
int blacklist (vector, vector, char *);
int blacklist_device (vector, vector, char *, char *);
int blacklist_path (struct config *, struct path *);
int store_ble (vector, char *, int);
int set_ble_device (vector, char *, char *, int);
void free_blacklist (vector);
void free_blacklist_device (vector);

#endif /* _BLACKLIST_H */
