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

int should_multipath(struct path *pp, vector pathvec, vector mpvec);
int remember_wwid(char *wwid);
int check_wwids_file(char *wwid, int write_wwid);
int remove_wwid(char *wwid);
int replace_wwids(vector mp);

enum {
	WWID_IS_NOT_FAILED = 0,
	WWID_IS_FAILED,
	WWID_FAILED_UNCHANGED,
	WWID_FAILED_CHANGED,
	WWID_FAILED_ERROR = -1,
};

int is_failed_wwid(const char *wwid);
int mark_failed_wwid(const char *wwid);
int unmark_failed_wwid(const char *wwid);
#endif /* _WWIDS_H */
