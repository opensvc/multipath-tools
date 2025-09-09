// SPDX-License-Identifier: GPL-2.0-or-later
/*
  Copyright (c) 2018 Martin Wilck, SUSE Linux GmbH
 */

#include "generic.h"
#include "structs.h"
#include "util.h"
#include "strbuf.h"

int generic_style(const struct gen_multipath* gm, struct strbuf *buf,
		  __attribute__((unused)) int verbosity)
{
	STRBUF_ON_STACK(tmp);
	char *alias_buf __attribute__((cleanup(cleanup_charp)));
	const char *wwid_buf;

	gm->ops->snprint(gm, &tmp, 'n');
	alias_buf = steal_strbuf_str(&tmp);
	gm->ops->snprint(gm, &tmp, 'w');
	wwid_buf = get_strbuf_str(&tmp);

	return print_strbuf(buf, "%%n %s[%%G]:%%d %%s",
			    strcmp(alias_buf, wwid_buf) ? "(%w) " : "");
}
