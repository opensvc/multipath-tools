#ifndef PRKEY_H_INCLUDED
#define PRKEY_H_INCLUDED

#include "structs.h"
#include <inttypes.h>

#define PRKEYS_FILE_HEADER \
"# Multipath persistent reservation keys, Version : 1.0\n" \
"# NOTE: this file is automatically maintained by the multipathd program.\n" \
"# You should not need to edit this file in normal circumstances.\n" \
"#\n" \
"# Format:\n" \
"# prkey wwid\n" \
"#\n"

int print_reservation_key(struct strbuf *buff,
			  struct be64 key, uint8_t flags, int source);
int parse_prkey_flags(char *ptr, uint64_t *prkey, uint8_t *flags);
int set_prkey(struct config *conf, struct multipath *mpp,
	      uint64_t prkey, uint8_t sa_flags);
int get_prkey(struct multipath *mpp, uint64_t *prkey, uint8_t *sa_flags);

#endif /* PRKEY_H_INCLUDED */
