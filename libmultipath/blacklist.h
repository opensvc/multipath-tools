#ifndef _BLACKLIST_H
#define _BLACKLIST_H

#include <libudev.h>
#include <regex.h>

#define MATCH_NOTHING        0
#define MATCH_WWID_BLIST     1
#define MATCH_DEVICE_BLIST   2
#define MATCH_DEVNODE_BLIST  3
#define MATCH_PROPERTY_BLIST 4
#define MATCH_PROPERTY_BLIST_MISSING 5
#define MATCH_PROTOCOL_BLIST 6
#define MATCH_WWID_BLIST_EXCEPT     -MATCH_WWID_BLIST
#define MATCH_DEVICE_BLIST_EXCEPT   -MATCH_DEVICE_BLIST
#define MATCH_DEVNODE_BLIST_EXCEPT  -MATCH_DEVNODE_BLIST
#define MATCH_PROPERTY_BLIST_EXCEPT -MATCH_PROPERTY_BLIST
#define MATCH_PROTOCOL_BLIST_EXCEPT -MATCH_PROTOCOL_BLIST

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
int filter_devnode (vector, vector, char *);
int filter_wwid (vector, vector, char *, char *);
int filter_device (vector, vector, char *, char *, char *);
int filter_path (struct config *, struct path *);
int filter_property(struct config *, struct udev_device *, int, const char*);
int filter_protocol(vector, vector, struct path *);
int store_ble (vector, char *, int);
int set_ble_device (vector, char *, char *, int);
void free_blacklist (vector);
void free_blacklist_device (vector);
void merge_blacklist(vector);
void merge_blacklist_device(vector);

#endif /* _BLACKLIST_H */
