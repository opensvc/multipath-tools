#ifndef BLACKLIST_H_INCLUDED
#define BLACKLIST_H_INCLUDED

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
	bool invert;
	int origin;
};

struct blentry_device {
	char * vendor;
	char * product;
	regex_t vendor_reg;
	regex_t product_reg;
	bool vendor_invert;
	bool product_invert;
	int origin;
};

int setup_default_blist (struct config *);
int alloc_ble_device (vector);
int filter_devnode (const struct vector_s *, const struct vector_s *,
		    const char *);
int filter_wwid (const struct vector_s *, const struct vector_s *,
		 const char *, const char *);
int filter_device (const struct vector_s *, const struct vector_s *,
		   const char *, const char *, const char *);
int filter_path (const struct config *, const struct path *);
int filter_property(const struct config *, struct udev_device *,
		    int, const char*);
int filter_protocol(const struct vector_s *, const struct vector_s *,
		    const struct path *);
int store_ble (vector, const char *, int);
int set_ble_device (vector, const char *, const char *, int);
void free_blacklist (vector);
void free_blacklist_device (vector);
void merge_blacklist(vector);
void merge_blacklist_device(vector);

#endif /* BLACKLIST_H_INCLUDED */
