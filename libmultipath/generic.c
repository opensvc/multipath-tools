/*
  Copyright (c) 2018 Martin Wilck, SUSE Linux GmbH

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
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
