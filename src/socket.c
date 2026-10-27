/*
 * Copyright (c) 2008-2016 Lu, Chao-Ming (Tetralet).  All rights reserved.
 *
 * GtkSocket is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * GtkSocket is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GtkSocket.  If not, see <http://www.gnu.org/licenses/>.
 */

// The Main Function Usage:
//
// Unix_Socket_States init_gtk_socket(gchar *package_name, gchar *socket_str, GSourceFunc read_function)
//
// package_name: The uniq name of package.
//		 The socket server will create an unix soket file at $XDG_RUNTIME_DIR/.PACKAGENAME_USERNAME:DISPLAY
//		 (or in /tmp, if there is no usable $XDG_RUNTIME_DIR)
// socket_str: A string will be sent to the socket server, if it exist.
// read_function: A function that will headle the socket_str as it's the socket server.
//		  gboolean read_function(gchar *socket_str)
//
// Example:
//	if (init_gtk_socket(PACKAGE, socket_str, (GSourceFunc)read_function) == UNIX_SOCKET_DATA_SENT)
//		/* Data is sent. Quiting... */
//
// return: UNIX_SOCKET_SERVER_INITED: had been inited as a unix socket server.
//	   UNIX_SOCKET_DATA_SENT: the data had been sent to an unix socket server.
//	   UNIX_SOCKET_ERROR: error occured.

//
// init_gtk_socket() -----------------(N)------------------------------------> return FALSE
//	|				^					^
//   init_socket_data()			|					|
//   socket_fd -------------------(N)---|					|
//   set_fd_non_block ------------(N)---/					|
//	|									|
// open_socket_lock(), flock: a running server holds the lock		|
//	|									|
// query_socket() (waits up to 5 s when a server runs)				|
//   connect ---------------------(N)-------> init_socket_server()--------------|
//					(only when no server runs, and	|
//					 the socket is gone or stale)	|
//	|				^	unlink				|
//   send_socket()			|	bind -----------------------(N)-|
//   (blocking, 10 s timeout)		|	listen ---------------------(N)-|
//   send, until all is sent -----(N)---/     * g_io_channel_unix_new ------(N)-|
//   close (EOF: request complete)		g_io_channel_set_encoding --(N)-|
//      |					g_io_add_watch -------------(N)-/
//      |					|
//      |					|---- accept_socket()
//      |					|	(condition)
//      |					|	accept
//      |					|	set_fd_non_block
//   exit()					|     * g_io_channel_unix_new
//						|	g_io_channel_set_encoding
//						|	g_io_add_watch_full (IN, HUP)
//						|		|
//						|		`-- read_socket()
//						|		    g_io_channel_read_chars, until EOF
//						|		    [READ_FUNC]
//						|		  + clear_channel()
//						|		    g_io_channel_shutdown
//						|		    (g_io_channel_unref)
//						\---- g_atexit()
//							shutdown_socket_server()
//							  unlink
//							+ clear_channel()
//							  g_io_channel_shutdown
//							  (g_io_channel_unref)

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <gtk/gtk.h>

#include <stdlib.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/stat.h>

#include "socket.h"

# define MAX_BACKLOG 100
// A request is a command line and an environment: far below this
# define MAX_REQUEST_SIZE (16 * 1024 * 1024)

typedef enum {
	GTK_SOCKET_ERROR_CREATE_SOCKET_FILE,
	GTK_SOCKET_ERROR_CONNECT_SOCKET,
	GTK_SOCKET_ERROR_BIND_SOCKET,
	GTK_SOCKET_ERROR_LISTEN_SOCKET,
	GTK_SOCKET_ERROR_WATCH_SOCKET,
	GTK_SOCKET_ERROR_ACCEPT_REQUEST,
	GTK_SOCKET_ERROR_RECEIVE_DATA,
	GTK_SOCKET_ERROR_FCNTL,
	GTK_SOCKET_ERROR_CHANNEL_ENCODING,
	GTK_SOCKET_ERROR_SHUTDOWN_CHANNEL,
	GTK_SOCKET_ERROR_WRITE_CHANNEL,
	GTK_SOCKET_ERROR_CREATE_CHANNEL,
	GTK_SOCKET_ERROR_WRITE_BUFFER,
	GTK_SOCKET_ERROR_GOT_NULL_STRING,
	GTK_SOCKET_ERROR_TMP_DIR,
} Unix_Socket_Error;

gboolean init_socket_fd();
gboolean init_socket_data(gchar *package_name);
gboolean set_fd_non_block(gint *fd);
gboolean query_socket(gint64 wait);
static gint open_socket_lock(void);
static void release_socket_lock(void);
gboolean send_socket(gchar *socket_str);
gboolean clear_channel(GIOChannel *channel, gboolean unref);
gboolean init_socket_server(GSourceFunc read_function);
gboolean accept_socket(GIOChannel *source, GIOCondition condition, GSourceFunc read_function);
// a request that is still coming in
struct Socket_Request
{
	GString *data;
	GSourceFunc read_function;
};
gboolean read_socket(GIOChannel *channel, GIOCondition condition, struct Socket_Request *request);
static void free_socket_request(struct Socket_Request *request);
gint shutdown_socket_server(gpointer data);
static void shutdown_socket_server_at_exit(void);
gboolean socket_fault(Unix_Socket_Error type, GError *error, GIOChannel *channel, gboolean unref);
static gboolean peer_is_same_user(gint fd);
static gboolean watch_socket_server(GSourceFunc read_function);

// for unix socket
gint socket_fd = 0;
struct sockaddr_un address = {0};
int address_len = -1;
GIOChannel *main_channel = NULL;
// the lock of the socket server, held as long as it runs
static gint lock_fd = -1;
// why query_socket() failed: the errno of connect(), EPERM for another user
static gint connect_errno = 0;

	// END: glib-2.31.2/glib/gutils.h: g_atexit () GLIB_DEPRECATED
	#define g_atexit atexit

// it will return UNIX_SOCKET_SERVER_INITED if succeed
Unix_Socket_States init_gtk_socket(gchar *package_name, gchar *socket_str, GSourceFunc read_function)
{
#ifdef DETAIL
	g_debug("! Launch init_gtk_socket() with package_name = %s, socket_str = %s", package_name, socket_str);
#endif
	// init socket data
	if (init_socket_data(package_name))
	{
		// A running socket server holds the lock. It may have just started
		// and not listen yet: then give it a few seconds.
		lock_fd = open_socket_lock();
		gboolean server_runs = FALSE;
		if ((lock_fd >= 0) && (flock(lock_fd, LOCK_EX | LOCK_NB) < 0))
		{
			if (errno == EWOULDBLOCK)
				server_runs = TRUE;
			else
				// no locks here (NFS?): do without
				release_socket_lock();
		}

		// trying to connect to an existing Socket Server
		if (query_socket(server_runs ? 5 * G_USEC_PER_SEC : 0))
		{
			// (a LilyTerm before 0.12.1 serves without the lock)
			release_socket_lock();
			// success, sent the socket_str then quit
			if (send_socket(socket_str))
			{
				exit (0);
				return UNIX_SOCKET_DATA_SENT;
			}
			// A server is there, but did not take the request: run on our
			// own. Becoming the server would take the socket away from it.
			return UNIX_SOCKET_ERROR;
		}

		// Replace the socket file only when no server is behind it: not the
		// one of a server that runs but is busy (EAGAIN), nor another user's.
		if ((! server_runs) && ((connect_errno == ENOENT) || (connect_errno == ECONNREFUSED)) &&
		    init_socket_server(read_function))
		{
			// the lock stays ours until we exit
			atexit(shutdown_socket_server_at_exit);
			return UNIX_SOCKET_SERVER_INITED;
		}
		release_socket_lock();
	}

	return UNIX_SOCKET_ERROR;
}

gboolean init_socket_fd(void)
{
#ifdef DETAIL
	g_debug("! Launch init_socket_fd()");
#endif
	GError *error = NULL;
	// init socket
	socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (socket_fd < 0)
		// main_channel is NULL, so that we don't need to launch clear_channel()
		return socket_fault(GTK_SOCKET_ERROR_CREATE_SOCKET_FILE, error, NULL, FALSE);
	return TRUE;
}

// it will return TRUE if init socket data successfully
gboolean init_socket_data(gchar *package_name)
{
#ifdef DETAIL
	g_debug("! Launch init_socket_data() to init %s socket!", PACKAGE);
#endif
	GError *error = NULL;

	// clean data first
	bzero(&address, sizeof(address));
	// init the address of socket
	address.sun_family = AF_UNIX;

	// Prefer $XDG_RUNTIME_DIR: it is owned by the user with mode 0700, so no one
	// else can squat the socket name or connect to it. A world writable /tmp
	// offers neither, and is only the fallback for systems without a runtime dir.
	const gchar *tmp_dir = g_getenv("XDG_RUNTIME_DIR");
	if ((tmp_dir==NULL) || (! g_path_is_absolute(tmp_dir)) ||
	    (! g_file_test(tmp_dir, G_FILE_TEST_IS_DIR)) || access(tmp_dir, W_OK | X_OK))
		tmp_dir = g_get_tmp_dir();

	if (tmp_dir)
		g_snprintf(address.sun_path, sizeof(address.sun_path), "%s/.%s_%s%s",
			   tmp_dir ,package_name, g_get_user_name(),
			   gdk_display_get_name(gdk_display_get_default()));
	else
		return socket_fault(GTK_SOCKET_ERROR_TMP_DIR, error, NULL, FALSE);

	// sun_path is 108 bytes on Linux, but only 104 on macOS and the BSDs
	address.sun_path[sizeof(address.sun_path)-1] = address.sun_path[sizeof(address.sun_path)-2] = '\0';
	address_len = sizeof(address);

	if (! init_socket_fd()) return FALSE;

	// set this socket is non-block
	return set_fd_non_block(&socket_fd);
}

// it will return TRUE if scucceed
gboolean set_fd_non_block(gint *fd)
{
#ifdef DETAIL
	if (fd)
		g_debug("! Launch set_fd_non_block() with fd = %d!", *fd);
	else
		g_debug("! Launch set_fd_non_block() with fd = (%p)!", fd);
#endif
#ifdef SAFEMODE
	if (fd==NULL) return FALSE;
#endif
	GError *error = NULL;
	gint flags = fcntl(*fd, F_GETFL, 0);
	if (fcntl(*fd, F_SETFL, O_NONBLOCK|flags) < 0)
		// main_channel is NULL, so that we don't need to launch clear_channel()
		return socket_fault(GTK_SOCKET_ERROR_FCNTL, error, NULL, FALSE);
	return TRUE;
}

// The socket server holds an flock() on SOCKET.lock as long as it runs, and
// the kernel drops it when the server dies. So a server that runs is known
// for sure, also when it cannot take a connection at once. Returns the open
// lock file, or -1 when there is none to use.
static gint open_socket_lock(void)
{
	gchar *lock_path = g_strdup_printf("%s.lock", address.sun_path);
	gint fd = open(lock_path, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
	g_free(lock_path);
	if (fd < 0) return -1;

	// in /tmp, another user may have made it first
	struct stat lock_stat;
	if ((fstat(fd, &lock_stat) < 0) || (! S_ISREG(lock_stat.st_mode)) || (lock_stat.st_uid != getuid()))
	{
		close(fd);
		return -1;
	}
	return fd;
}

static void release_socket_lock(void)
{
	if (lock_fd >= 0) close(lock_fd);
	lock_fd = -1;
}

// it will return TRUE if scucceed. A server that runs may not listen yet, or
// have a full backlog (EAGAIN on Linux, ECONNREFUSED on macOS): try again for
// up to WAIT microseconds. connect_errno says why it failed.
gboolean query_socket(gint64 wait)
{
#ifdef DETAIL
	g_debug("! Launch query_socket() to connect to an existing %s !", PACKAGE);
#endif
	GError *error = NULL;
	gint64 deadline = g_get_monotonic_time() + wait;

	while (connect(socket_fd, (struct sockaddr *)&address, address_len) < 0)
	{
		connect_errno = errno;
		if ((g_get_monotonic_time() >= deadline) ||
		    ((errno != ENOENT) && (errno != ECONNREFUSED) && (errno != EAGAIN)))
			// main_channel is NULL, so that we don't need to launch clear_channel()
			return socket_fault(GTK_SOCKET_ERROR_CONNECT_SOCKET, error, NULL, FALSE);

		// the socket is unspecified after a failed connect(): use a new one
		close(socket_fd);
		g_usleep(50000);
		if ((! init_socket_fd()) || (! set_fd_non_block(&socket_fd)))
			return FALSE;
	}
	connect_errno = 0;

	// The request carries our whole environment: hand it only to a server of
	// our own user, not to whoever created the socket first (in /tmp, without
	// a $XDG_RUNTIME_DIR). Then this LilyTerm runs on its own.
	if (! peer_is_same_user(socket_fd))
	{
		g_warning("The %s socket \"%s\" does not belong to uid %d: not using it.",
			  PACKAGE, address.sun_path, (gint) getuid());
		connect_errno = EPERM;
		close(socket_fd);
		socket_fd = -1;
		address_len = -1;
		return FALSE;
	}
	return TRUE;
}

// TRUE if the other end of the unix socket FD runs as our own user
static gboolean peer_is_same_user(gint fd)
{
#ifdef SO_PEERCRED
	struct ucred peer;
	socklen_t peer_len = sizeof(peer);
	return (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &peer_len) == 0) &&
	       (peer_len == sizeof(peer)) && (peer.uid == getuid());
#else
	// macOS and the BSDs
	uid_t peer_uid;
	gid_t peer_gid;
	return (getpeereid(fd, &peer_uid, &peer_gid) == 0) && (peer_uid == getuid());
#endif
}

// it will return TRUE if scucceed
gboolean send_socket(gchar *socket_str)
{
#ifdef DETAIL
	g_debug("! Launch send_socket() to send data to the exiting %s with socket_str = \"%s\"!", PACKAGE, socket_str);
#endif
	// write data!
#ifdef SAFEMODE
	if (socket_str==NULL) return FALSE;
	if (fcntl(socket_fd, F_GETFL) < 0) return FALSE;
#endif
	// A request with a big environment does not fit in the socket buffer
	// (8 KB on macOS): wait for the server to read it, but not for longer
	// than 10 s in all. The NUL at the end tells the server it got all of it.
	// A server that went away must not kill us with SIGPIPE.
#ifdef SO_NOSIGPIPE
	gint on = 1;
	setsockopt(socket_fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
#ifndef MSG_NOSIGNAL
#  define MSG_NOSIGNAL 0
#endif
	gint64 deadline = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
	gsize total = strlen(socket_str) + 1;
	gsize sent = 0;
	while (sent < total)
	{
		ssize_t len = send(socket_fd, socket_str + sent, total - sent, MSG_NOSIGNAL);
		if (len >= 0)
		{
			sent += len;
			continue;
		}
		if (errno == EINTR) continue;
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
		{
			gint64 timeout = (deadline - g_get_monotonic_time()) / 1000;
			if (timeout > 0)
			{
				struct pollfd writable = { socket_fd, POLLOUT, 0 };
				poll(&writable, 1, (gint) timeout);
				continue;
			}
			errno = ETIMEDOUT;
		}
		GError *error = g_error_new_literal(G_FILE_ERROR, g_file_error_from_errno(errno),
						    g_strerror(errno));
		close(socket_fd);
		socket_fd = -1;
		return socket_fault(GTK_SOCKET_ERROR_WRITE_CHANNEL, error, NULL, FALSE);
	}

	// So far so good. Hang up: that tells the server the request is complete.
	close(socket_fd);
	socket_fd = -1;

	return TRUE;
}

// it will return TRUE if scucceed
gboolean clear_channel(GIOChannel *channel, gboolean unref)
{
#ifdef DETAIL
	g_debug("! Launch clear_channel() to clear channel data !");
#endif
	if (channel == NULL) return TRUE;

	gboolean return_value = TRUE;
	GError *error = NULL;

	if (g_io_channel_shutdown(channel, TRUE, &error) == G_IO_STATUS_ERROR)
		return_value = socket_fault(GTK_SOCKET_ERROR_SHUTDOWN_CHANNEL, error, NULL, FALSE);

	if (return_value && unref)
	{
		g_io_channel_unref(channel);
		channel = NULL;
	}

	return return_value;
}

// it will return TRUE if succeed
gboolean init_socket_server(GSourceFunc read_function)
{
#ifdef DETAIL
	g_debug("! Launch init_socket_server() to init a %s socket server !", PACKAGE);
#endif
	GError *error = NULL;

	// clear the prev file
	if (address.sun_path[0]) unlink(address.sun_path);

	// fchmod() on socket may cause unspecified behaviour.

	// bind the socket on a file
	if (bind(socket_fd, (struct sockaddr *)&address, address_len) < 0)
		// main_channel is NULL, so that we don't need to launch clear_channel()
		return socket_fault(GTK_SOCKET_ERROR_BIND_SOCKET, error, NULL, FALSE);

	// create socket queue
	if (listen(socket_fd, MAX_BACKLOG) < 0)
		// main_channel is NULL, so that we don't need to launch clear_channel()
		return socket_fault(GTK_SOCKET_ERROR_LISTEN_SOCKET, error, NULL, FALSE);

	main_channel = g_io_channel_unix_new(socket_fd);
	if (!main_channel) return socket_fault(GTK_SOCKET_ERROR_CREATE_CHANNEL, NULL, NULL, FALSE);

	// set the channel to read binary file
	if (g_io_channel_set_encoding(main_channel, NULL, &error) == G_IO_STATUS_ERROR)
		return socket_fault(GTK_SOCKET_ERROR_CHANNEL_ENCODING, error, main_channel, TRUE);

	// the channel will be clean when shutdown_socket_server()
	watch_socket_server(read_function);

	return TRUE;
}

// (again) accept the requests on the listening channel. Returns FALSE, so that
// it can be a timeout function too.
static gboolean watch_socket_server(GSourceFunc read_function)
{
	if (main_channel==NULL) return FALSE;
	g_io_add_watch(main_channel, G_IO_IN|G_IO_HUP, (GIOFunc)accept_socket, read_function);
	return FALSE;
}

// it will return TRUE if succeed
gboolean accept_socket(GIOChannel *source, GIOCondition condition, GSourceFunc read_function)
{
#ifdef DETAIL
	g_debug("! Launch accept_socket() to accept the request from client !");
#endif
#ifdef SAFEMODE
	if (source==NULL) return FALSE;
#endif
	GError *error = NULL;

	if (condition & G_IO_HUP)
		return socket_fault(GTK_SOCKET_ERROR_ACCEPT_REQUEST, error, source, FALSE);
	else
	{
		gint read_fd = accept(g_io_channel_unix_get_fd(source), NULL, NULL);

		// A failed request must not take the socket server down with it
		// (source is the listening channel).
		if (read_fd < 0)
		{
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR) || (errno == ECONNABORTED))
				return TRUE;
			// Out of file descriptors (EMFILE, ENFILE): the request stays in
			// the queue, and the watch would fire again at once. Pause.
			socket_fault(GTK_SOCKET_ERROR_ACCEPT_REQUEST, error, NULL, FALSE);
			g_timeout_add_seconds(1, (GSourceFunc)watch_socket_server, read_function);
			return FALSE;
		}

		// The request ends up as a command line that is executed with our
		// privileges: never take one from another user.
		if (! peer_is_same_user(read_fd))
		{
			g_warning("Refused a request on the %s socket: it does not come from uid %d.",
				  PACKAGE, (gint) getuid());
			close(read_fd);
			// keep the socket server alive
			return TRUE;
		}

		if ( ! set_fd_non_block(&read_fd))
		{
			close(read_fd);
			return TRUE;
		}

		GIOChannel* channel = g_io_channel_unix_new(read_fd);
		if (!channel)
		{
			socket_fault(GTK_SOCKET_ERROR_CREATE_CHANNEL, NULL, NULL, FALSE);
			close(read_fd);
			return TRUE;
		}
		// set the channel to read binary file
		if (g_io_channel_set_encoding(channel, NULL, &error) == G_IO_STATUS_ERROR)
		{
			socket_fault(GTK_SOCKET_ERROR_CHANNEL_ENCODING, error, channel, TRUE);
			return TRUE;
		}

		// read the data that client sent, as it comes in.
		struct Socket_Request *request = g_new0(struct Socket_Request, 1);
		request->data = g_string_new(NULL);
		request->read_function = read_function;
		if ( ! g_io_add_watch_full(channel, G_PRIORITY_DEFAULT, G_IO_IN|G_IO_HUP|G_IO_ERR,
					   (GIOFunc)read_socket, request, (GDestroyNotify)free_socket_request))
		{
			// (it never fails: GLib aborts first)
			socket_fault(GTK_SOCKET_ERROR_WATCH_SOCKET, NULL, channel, TRUE);
			return TRUE;
		}
	}
	return TRUE;
}

// It returns TRUE while the request is still coming in.
gboolean read_socket(GIOChannel *channel, GIOCondition condition, struct Socket_Request *request)
{
#ifdef DETAIL
	g_debug("! Launch read_socket() to read data !");
#endif
#ifdef SAFEMODE
	if ((channel==NULL) || (request==NULL)) return FALSE;
#endif
	GError *error = NULL;
	gchar buffer[4096];
	gsize len = 0;
	GIOStatus status;

	// Read what is there now. The client hangs up once everything is sent
	// (EOF): only then the request is complete. It may hold newlines (in an
	// argument, or in a variable of the environment).
	do
	{
		status = g_io_channel_read_chars(channel, buffer, sizeof(buffer), &len, &error);
		if (len) g_string_append_len(request->data, buffer, len);
	}
	while ((status == G_IO_STATUS_NORMAL) && (request->data->len <= MAX_REQUEST_SIZE));

	if (status == G_IO_STATUS_ERROR)
		// socket_fault() shuts the channel down and unrefs it: do not touch it again
		return socket_fault(GTK_SOCKET_ERROR_RECEIVE_DATA, error, channel, TRUE);

	if (request->data->len > MAX_REQUEST_SIZE)
	{
		g_warning("Refused a request of more than %d bytes on the %s socket.", MAX_REQUEST_SIZE, PACKAGE);
		clear_channel(channel, TRUE);
		return FALSE;
	}

	// nothing more for now, and the client is still there: wait for the rest
	if ((status == G_IO_STATUS_AGAIN) && (! (condition & (G_IO_HUP|G_IO_ERR))))
		return TRUE;

	// read_socket_functions... A request ends with a NUL: without it, the
	// client went away before it sent all (or is a LilyTerm before 0.12.0).
	// Do not run what is left of it: a command line that is cut short.
	if (request->data->len == 0)
		socket_fault(GTK_SOCKET_ERROR_GOT_NULL_STRING, NULL, NULL, FALSE);
	else if (request->data->str[request->data->len - 1] != '\0')
		g_warning("Dropped an incomplete request on the %s socket (%lu bytes). "
			  "If it came from an older %s, please restart %s.",
			  PACKAGE, (gulong) request->data->len, PACKAGE, PACKAGE);
	else
		request->read_function(request->data->str);

	clear_channel(channel, TRUE);

	// return FALSE means this connection is finished.
	return FALSE;
}

static void free_socket_request(struct Socket_Request *request)
{
	if (request==NULL) return;
	g_string_free(request->data, TRUE);
	g_free(request);
}

// g_atexit() is gone, and atexit() wants a real void (*)(void).
static void shutdown_socket_server_at_exit(void)
{
	shutdown_socket_server(NULL);
}

// It should always return 0.
gint shutdown_socket_server(gpointer data)
{
#ifdef DETAIL
	g_debug("! Launch shutdown_socket_server() to shutdown the %s socket server!", PACKAGE);
#endif
	if (main_channel) clear_channel(main_channel, TRUE);
	if (address.sun_path[0]) unlink(address.sun_path);
	return 0;
}

// it will always return FALSE
gboolean socket_fault(Unix_Socket_Error type, GError *error, GIOChannel *channel, gboolean unref)
{
#ifdef DETAIL
	g_debug("! Launch socket_fault(%d) to show the error message !", type);
#endif
#  define G_WARNING g_warning
	switch (type)
	{
		case GTK_SOCKET_ERROR_CREATE_SOCKET_FILE:
			G_WARNING("Error when create the socket file \"%s\": %s",
				  address.sun_path, g_strerror (errno));
			break;
		case GTK_SOCKET_ERROR_CONNECT_SOCKET:
			// No socket file, or a stale one without a server behind it:
			// that is simply the first LilyTerm starting. Not worth a word.
			if ((errno != ENOENT) && (errno != ECONNREFUSED))
				g_message("Can NOT connect to a existing unix socket: %s", g_strerror (errno));
			break;
		case GTK_SOCKET_ERROR_BIND_SOCKET:
			G_WARNING("Can NOT bind on the socket!");
			break;
		case GTK_SOCKET_ERROR_LISTEN_SOCKET:
			G_WARNING("Can NOT listen on the socket!");
			break;
		case GTK_SOCKET_ERROR_WATCH_SOCKET:
			G_WARNING("Can not watch on the socket!");
			break;
		case GTK_SOCKET_ERROR_ACCEPT_REQUEST:
			G_WARNING("Error when accepting client request via socket!");
			break;
		case GTK_SOCKET_ERROR_RECEIVE_DATA:
			G_WARNING("Error when reading the data client sent via socket!");
			break;
		case GTK_SOCKET_ERROR_FCNTL:
			G_WARNING("Error when running fcntl command on socket!");
			break;
		case GTK_SOCKET_ERROR_CHANNEL_ENCODING:
			if (error)
				G_WARNING("Error when setting the encoding of channel: %s", error->message);
			else
				G_WARNING("Error when setting the encoding of channel: (No further information)");
			break;
		case GTK_SOCKET_ERROR_SHUTDOWN_CHANNEL:
			if (error)
				G_WARNING("Error when shutdowning a channel: %s", error->message);
			else
				G_WARNING("Error when shutdowning a channel: (No further information)");
			break;
		case GTK_SOCKET_ERROR_WRITE_CHANNEL:
			if (error)
				G_WARNING("Error when writing data to the channel: %s", error->message);
			else
				G_WARNING("Error when writing data to the channel: (No further information)");
			break;
		case GTK_SOCKET_ERROR_CREATE_CHANNEL:
			G_WARNING("Can NOT create a channel for this socket");
			break;
		case GTK_SOCKET_ERROR_WRITE_BUFFER:
			if (error)
				G_WARNING("Error when flushing the write buffer for the channel: %s", error->message);
			else
				G_WARNING("Error when flushing the write buffer for the channel: (No further information)");
			break;
		case GTK_SOCKET_ERROR_GOT_NULL_STRING:
			G_WARNING("Got a NULL string from the socket!");
			break;
		case GTK_SOCKET_ERROR_TMP_DIR:
			G_WARNING("The /tmp dir is NOT exist!!");
			break;
		default:
#ifdef FATAL
			G_WARNING("The inputed %d for socket_fault() is Out of Range !!", type);
#endif
			break;
	}

	if (error) g_clear_error (&error);
	clear_channel(channel, unref);

	if (type != GTK_SOCKET_ERROR_CONNECT_SOCKET) address_len = -1;

	return FALSE;
}
