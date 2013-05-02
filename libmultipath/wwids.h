/*
 * Copyright (c) 2010 Benjamin Marzinski, Redhat
 */

#ifndef _WWIDS_H
#define _WWIDS_H

#define WWIDS_FILE_HEADER \
"# Multipath wwids, Version : 1.0\n" \
"# NOTE: This file is automatically maintained by multipath and multipathd.\n" \
"# You should not need to edit this file in normal circumstances.\n" \
"#\n" \
"# Valid WWIDs:\n"

int remember_wwid(char *wwid);
int check_wwids_file(char *wwid, int write_wwid);
int remove_wwid(char *wwid);
int replace_wwids(vector mp);

#endif /* _WWIDS_H */
