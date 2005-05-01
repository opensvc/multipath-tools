#define CACHE_FILE	"/var/cache/multipath/.multipath.cache"
#define CACHE_TMPFILE	"/var/cache/multipath/.multipath.cache.tmp"
#define CACHE_EXPIRE	5
#define MAX_WAIT	5

int cache_load (vector pathvec);
int cache_dump (vector pathvec);
int cache_cold (int expire);
