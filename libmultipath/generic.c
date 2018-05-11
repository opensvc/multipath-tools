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


#include <string.h>
#include "generic.h"
#include "structs.h"

int generic_style(const struct gen_multipath* gm,
		  char *buf, int len, int verbosity)
{
	char alias_buf[WWID_SIZE];
	char wwid_buf[WWID_SIZE];
	int n = 0;

	gm->ops->snprint(gm, alias_buf, sizeof(alias_buf), 'n');
	gm->ops->snprint(gm, wwid_buf, sizeof(wwid_buf), 'w');

	n += snprintf(buf, len, "%%n %s[%%G]:%%d %%s",
		      strcmp(alias_buf, wwid_buf) ? "(%w) " : "");

	return (n < len ? n : len - 1);
}
