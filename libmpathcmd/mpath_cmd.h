/*
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * This file is part of the device-mapper multipath userspace tools.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIB_MPATH_CMD_H
#define LIB_MPATH_CMD_H

/*
 * This should be sufficient for json output for >10000 maps,
 * and >60000 paths.
 */
#define MAX_REPLY_LEN (32 * 1024 * 1024)

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_SOCKET		"/org/kernel/linux/storage/multipathd"
#define DEFAULT_REPLY_TIMEOUT	4000


/*
 * DESCRIPTION:
 *	Same as mpath_connect() (see below) except for the "nonblocking"
 *	parameter.
 *	If "nonblocking" is set, connects in non-blocking mode. This is
 *	useful to avoid blocking if the listening socket's backlog is
 *	exceeded. In this case, errno will be set to EAGAIN.
 *	In case of success, the returned file descriptor is in in blocking
 *	mode, even if "nonblocking" was true.
 *
 * RETURNS:
 *	A file descriptor on success. -1 on failure (with errno set).
 */
int __mpath_connect(int nonblocking);

/*
 * DESCRIPTION:
 *	Connect to the running multipathd daemon. On systems with the
 *	multipathd.socket systemd unit file installed, this command will
 *	start multipathd if it is not already running. This function
 *	must be run before any of the others in this library
 *
 * RETURNS:
 *	A file descriptor on success. -1 on failure (with errno set).
 */
int mpath_connect(void);


/*
 * DESCRIPTION:
 *	Disconnect from the multipathd daemon. This function must be
 *	run after after processing all the multipath commands.
 *
 * RETURNS:
 *	0 on success. -1 on failure (with errno set).
 */
int mpath_disconnect(int fd);


/*
 * DESCRIPTION
 *	Send multipathd a command and return the reply. This function
 *	does the same as calling mpath_send_cmd() and then
 *	mpath_recv_reply()
 *
 * RETURNS:
 *	0 on successs, and reply will either be NULL (if there was no
 *	reply data), or point to the reply string, which must be freed by
 *	the caller. -1 on failure (with errno set).
 */
int mpath_process_cmd(int fd, const char *cmd, char **reply,
		      unsigned int timeout);


/*
 * DESCRIPTION:
 *	Send a command to multipathd
 *
 * RETURNS:
 *	0 on success. -1 on failure (with errno set)
 */
int mpath_send_cmd(int fd, const char *cmd);


/*
 * DESCRIPTION:
 *	Return a reply from multipathd for a previously sent command.
 *	This is equivalent to calling mpath_recv_reply_len(), allocating
 *	a buffer of the appropriate size, and then calling
 *	mpath_recv_reply_data() with that buffer.
 *
 * RETURNS:
 *	0 on success, and reply will either be NULL (if there was no
 *	reply data), or point to the reply string, which must be freed by
 *	the caller, -1 on failure (with errno set).
 */
int mpath_recv_reply(int fd, char **reply, unsigned int timeout);


/*
 * DESCRIPTION:
 *	Return the size of the upcoming reply data from the sent multipath
 *	command. This must be called before calling mpath_recv_reply_data().
 *
 * RETURNS:
 *	The required size of the reply data buffer on success. -1 on
 *	failure (with errno set).
 */
ssize_t mpath_recv_reply_len(int fd, unsigned int timeout);


/*
 * DESCRIPTION:
 *	Return the reply data from the sent multipath command.
 *	mpath_recv_reply_len must be called first. reply must point to a
 *	buffer of len size.
 *
 * RETURNS:
 *	0 on success, and reply will contain the reply data string. -1
 *	on failure (with errno set).
 */
int mpath_recv_reply_data(int fd, char *reply, size_t len,
			  unsigned int timeout);

#ifdef __cplusplus
}
#endif
#endif /* LIB_MPATH_CMD_H */
