/* -*- mode: c; c-file-style: "bsd"; -*- */
/*
    Copyright (C) 2001-2005 Paul Davis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 */

#include <config.h>

#include <stdio.h>
#include <ctype.h>
#include <signal.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <dirent.h>
#include <dlfcn.h>

#include <jack/midiport.h>
#include <jack/intclient.h>
#include <jack/uuid.h>

#include "engine.h"
#include "internal.h"
#include "driver.h"
#include "shm.h"
#include "driver_parse.h"
#include "messagebuffer.h"
#include "clientengine.h"

#ifdef USE_CAPABILITIES

#include <sys/stat.h>
/* capgetp and capsetp are linux only extensions, not posix */
#undef _POSIX_SOURCE
#include <sys/capability.h>
#include "start.h"

static struct stat pipe_stat;

#endif /* USE_CAPABILITIES */

static JSList *drivers = NULL;
static sigset_t signals;
static jack_engine_t *engine = NULL;
static char *server_name = NULL;
static int realtime = 1;
static int realtime_priority = 10;
static int do_mlock = 1;
static int temporary = 0;
static int verbose = 0;
static int client_timeout = 0; /* msecs; if zero, use period size. */
static unsigned int port_max = 256;
static int do_unlock = 0;
static jack_nframes_t frame_time_offset = 0;
static int nozombies = 0;
static int timeout_count_threshold = 0;

extern int sanitycheck(int, int);

static jack_driver_desc_t *
jack_find_driver_descriptor(const char * name);

static void
do_nothing_handler (int sig)
{
	/* this is used by the child (active) process, but it never
	   gets called unless we are already shutting down after
	   another signal.
	 */
	char buf[64];

	snprintf (buf, sizeof(buf),
		  "received signal %d during shutdown (ignored)\n", sig);
	write (1, buf, strlen (buf));
}

static void
jack_load_internal_clients (JSList* load_list)
{
	JSList * node;

	for (node = load_list; node; node = jack_slist_next (node)) {

		char* str = (char*)node->data;
		jack_request_t req;
		char* colon = strchr (str, ':');
		char* slash = strchr (str, '/');
		char* client_name = NULL;
		char* path = NULL;
		char* args = NULL;
		char* rest = NULL;
		int free_path = 0;
		int free_name = 0;
		size_t len;

		/* possible argument forms:

		   client-name:client-type/args
		   client-type/args
		   client-name:client-type
		   client-type

		   client-name is the desired JACK client name.
		   client-type is basically the name of the DLL/DSO without any suffix.
		   args is a string whose contents will be passed to the client as
		   it is instantiated
		 */

		if ((slash == NULL && colon == NULL) || (slash && colon && colon > slash)) {

			/* client-type */

			client_name = str;
			path = client_name;

		} else if (slash && colon) {

			/* client-name:client-type/args */

			len = colon - str;
			if (len) {
				/* add 1 to leave space for a NULL */
				client_name = (char*)malloc (len + 1);
				free_name = 1;
				memcpy (client_name, str, len);
				client_name[len] = '\0';
			}

			len = slash - (colon + 1);
			if (len) {
				/* add 1 to leave space for a NULL */
				path = (char*)malloc (len + 1);
				free_path = 1;
				memcpy (path, colon + 1, len);
				path[len] = '\0';
			} else {
				path = client_name;
			}

			rest = slash + 1;
			len = strlen (rest);

			if (len) {
				/* add 1 to leave space for a NULL */
				args = (char*)malloc (len + 1);
				memcpy (args, rest, len);
				args[len] = '\0';
			}

		} else if (slash && colon == NULL) {

			/* client-type/args */

			len = slash - str;

			if (len) {
				/* add 1 to leave space for a NULL */
				path = (char*)malloc (len + 1);
				free_path = 1;
				memcpy (path, str, len);
				path[len] = '\0';
			}

			rest = slash + 1;
			len = strlen (rest);

			if (len) {
				/* add 1 to leave space for a NULL */
				args = (char*)malloc (len + 1);
				memcpy (args, rest, len);
				args[len] = '\0';
			}
		} else {

			/* client-name:client-type */

			len = colon - str;

			if (len) {
				/* add 1 to leave space for a NULL */
				client_name = (char*)malloc (len + 1);
				free_name = 1;
				memcpy (client_name, str, len);
				client_name[len] = '\0';
				path = colon + 1;
			}
		}

		if (client_name == NULL || path == NULL) {
			fprintf (stderr, "incorrect format for internal client specification (%s)\n", str);
			exit (1);
		}

		memset (&req, 0, sizeof(req));
		req.type = IntClientLoad;
		req.x.intclient.options = 0;
		strncpy (req.x.intclient.name, client_name, sizeof(req.x.intclient.name));
		strncpy (req.x.intclient.path, path, sizeof(req.x.intclient.path));

		if (args) {
			strncpy (req.x.intclient.init, args, sizeof(req.x.intclient.init));
		} else {
			req.x.intclient.init[0] = '\0';
		}

		pthread_mutex_lock (&engine->request_lock);
		jack_intclient_load_request (engine, &req);
		pthread_mutex_unlock (&engine->request_lock);

		if (free_name) {
			free (client_name);
		}
		if (free_path) {
			free (path);
		}
		if (args) {
			free (args);
		}
	}
}

static int
jack_main (jack_driver_desc_t * driver_desc, JSList * driver_params, JSList * slave_names, JSList * load_list)
{
	int sig;
	int i;
	sigset_t allsignals;
	struct sigaction action;
	int waiting;
	JSList * node;

	/* ensure that we are in our own process group so that
	   kill (SIG, -pgrp) does the right thing.
	 */

	setsid ();

	pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	/* what's this for?

	   POSIX says that signals are delivered like this:

	 * if a thread has blocked that signal, it is not
	       a candidate to receive the signal.
	 * of all threads not blocking the signal, pick
	       one at random, and deliver the signal.

	   this means that a simple-minded multi-threaded program can
	   expect to get POSIX signals delivered randomly to any one
	   of its threads,

	   here, we block all signals that we think we might receive
	   and want to catch. all "child" threads will inherit this
	   setting. if we create a thread that calls sigwait() on the
	   same set of signals, implicitly unblocking all those
	   signals. any of those signals that are delivered to the
	   process will be delivered to that thread, and that thread
	   alone. this makes cleanup for a signal-driven exit much
	   easier, since we know which thread is doing it and more
	   importantly, we are free to call async-unsafe functions,
	   because the code is executing in normal thread context
	   after a return from sigwait().
	 */

	sigemptyset (&signals);
	sigaddset (&signals, SIGHUP);
	sigaddset (&signals, SIGINT);
	sigaddset (&signals, SIGQUIT);
	sigaddset (&signals, SIGPIPE);
	sigaddset (&signals, SIGTERM);
	sigaddset (&signals, SIGUSR1);
	sigaddset (&signals, SIGUSR2);

	/* all child threads will inherit this mask unless they
	 * explicitly reset it
	 */

	pthread_sigmask (SIG_BLOCK, &signals, 0);

	if (!realtime && client_timeout == 0) {
		client_timeout = 500; /* 0.5 sec; usable when non realtime. */

	}
	/* get the engine/driver started */

	if ((engine = jack_engine_new (realtime, realtime_priority,
				       do_mlock, do_unlock, server_name,
				       temporary, verbose, client_timeout,
				       port_max, getpid (), frame_time_offset,
				       nozombies, timeout_count_threshold, drivers)) == 0) {
		jack_error ("cannot create engine");
		return -1;
	}

	jack_info ("loading driver ..");

	if (jack_engine_load_driver (engine, driver_desc, driver_params)) {
		jack_error ("cannot load driver module %s",
			    driver_desc->name);
		goto error;
	}

	for (node = slave_names; node; node = jack_slist_next (node)) {
		char *sl_name = node->data;
		jack_driver_desc_t *sl_desc = jack_find_driver_descriptor (sl_name);
		if (sl_desc) {
			jack_engine_load_slave_driver (engine, sl_desc, NULL);
		}
	}


	if (jack_drivers_start (engine) != 0) {
		jack_error ("cannot start driver");
		goto error;
	}

	jack_load_internal_clients (load_list);

	/* install a do-nothing handler because otherwise pthreads
	   behaviour is undefined when we enter sigwait.
	 */

	sigfillset (&allsignals);
	action.sa_handler = do_nothing_handler;
	action.sa_mask = allsignals;
	action.sa_flags = SA_RESTART | SA_RESETHAND;

	for (i = 1; i < NSIG; i++) {
		if (sigismember (&signals, i)) {
			sigaction (i, &action, 0);
		}
	}

	if (verbose) {
		jack_info ("%d waiting for signals", getpid ());
	}

	waiting = TRUE;

	while (waiting) {
		sigwait (&signals, &sig);

		jack_info ("jack main caught signal %d", sig);

		switch (sig) {
		case SIGUSR1:
			jack_dump_configuration (engine, 1);
			break;
		case SIGUSR2:
			/* driver exit */
			waiting = FALSE;
			break;
		default:
			waiting = FALSE;
			break;
		}
	}

	if (sig != SIGSEGV) {

		/* unblock signals so we can see them during shutdown.
		   this will help prod developers not to lose sight of
		   bugs that cause segfaults etc. during shutdown.
		 */
		sigprocmask (SIG_UNBLOCK, &signals, 0);
	}

	jack_engine_delete (engine);
	return 1;

error:
	jack_engine_delete (engine);
	return -1;
}

static jack_driver_desc_t *
jack_drivers_get_descriptor (JSList * drivers, const char * sofile)
{
	jack_driver_desc_t * descriptor, * other_descriptor;
	JackDriverDescFunction so_get_descriptor;
	JSList * node;
	void * dlhandle;
	char * filename;
	const char * dlerr;
	int err;
	char* driver_dir;

	if ((driver_dir = getenv ("JACK_DRIVER_DIR")) == 0) {
		driver_dir = ADDON_DIR;
	}
	filename = malloc (strlen (driver_dir) + 1 + strlen (sofile) + 1);
	sprintf (filename, "%s/%s", driver_dir, sofile);

	if (verbose) {
		jack_info ("getting driver descriptor from %s", filename);
	}

	if ((dlhandle = dlopen (filename, RTLD_NOW | RTLD_GLOBAL)) == NULL) {
		jack_error ("could not open driver .so '%s': %s\n", filename, dlerror ());
		free (filename);
		return NULL;
	}

	so_get_descriptor = (JackDriverDescFunction)
			    dlsym (dlhandle, "driver_get_descriptor");

	if ((dlerr = dlerror ()) != NULL) {
		jack_error ("%s", dlerr);
		dlclose (dlhandle);
		free (filename);
		return NULL;
	}

	if ((descriptor = so_get_descriptor ()) == NULL) {
		jack_error ("driver from '%s' returned NULL descriptor\n", filename);
		dlclose (dlhandle);
		free (filename);
		return NULL;
	}

	if ((err = dlclose (dlhandle)) != 0) {
		jack_error ("error closing driver .so '%s': %s\n", filename, dlerror ());
	}

	/* check it doesn't exist already */
	for (node = drivers; node; node = jack_slist_next (node)) {
		other_descriptor = (jack_driver_desc_t*)node->data;

		if (strcmp (descriptor->name, other_descriptor->name) == 0) {
			jack_error ("the drivers in '%s' and '%s' both have the name '%s'; using the first\n",
				    other_descriptor->file, filename, other_descriptor->name);
			/* FIXME: delete the descriptor */
			free (filename);
			return NULL;
		}
	}

	snprintf (descriptor->file, sizeof(descriptor->file), "%s", filename);
	free (filename);

	return descriptor;
}

static JSList *
jack_drivers_load ()
{
	struct dirent * dir_entry;
	DIR * dir_stream;
	const char * ptr;
	int err;
	JSList * driver_list = NULL;
	jack_driver_desc_t * desc;
	char* driver_dir;

	if ((driver_dir = getenv ("JACK_DRIVER_DIR")) == 0) {
		driver_dir = ADDON_DIR;
	}

	/* search through the driver_dir and add get descriptors
	   from the .so files in it */
	dir_stream = opendir (driver_dir);
	if (!dir_stream) {
		jack_error ("could not open driver directory %s: %s\n",
			    driver_dir, strerror (errno));
		return NULL;
	}

	while ( (dir_entry = readdir (dir_stream)) ) {
		/* check the filename is of the right format */
		if (strncmp ("jack_", dir_entry->d_name, 5) != 0) {
			continue;
		}

		ptr = strrchr (dir_entry->d_name, '.');
		if (!ptr) {
			continue;
		}
		ptr++;
		if (strncmp ("so", ptr, 2) != 0) {
			continue;
		}

		desc = jack_drivers_get_descriptor (drivers, dir_entry->d_name);
		if (desc) {
			driver_list = jack_slist_append (driver_list, desc);
		}
	}

	err = closedir (dir_stream);
	if (err) {
		jack_error ("error closing driver directory %s: %s\n",
			    driver_dir, strerror (errno));
	}

	if (!driver_list) {
		jack_error ("could not find any drivers in %s!\n", driver_dir);
		return NULL;
	}

	return driver_list;
}

static void copyright (FILE* file)
{
	fprintf (file, "jackd " VERSION "\n"
		 "Copyright 2001-2009 Paul Davis, Stephane Letz, Jack O'Quinn, Torben Hohn and others.\n"
		 "jackd comes with ABSOLUTELY NO WARRANTY\n"
		 "This is free software, and you are welcome to redistribute it\n"
		 "under certain conditions; see the file COPYING for details\n\n");
}

static void usage (FILE *file)
{
	copyright (file);
	fprintf (file, "\n"
		 "usage: jackd [ server options ] -d backend [ ... backend options ... ]\n"
		 "             (see the manual page for jackd for a complete list of options)\n\n"
#ifdef __APPLE__
		 "             Available backends may include: coreaudio, dummy, net, portaudio.\n\n"
#else
		 "             Available backends may include: alsa, dummy, freebob, firewire, net, oss, sun, portaudio or sndio.\n\n"
#endif
		 "       jackd -d backend --help\n"
		 "             to display options for each backend\n\n");
}

static jack_driver_desc_t *
jack_find_driver_descriptor (const char * name)
{
	jack_driver_desc_t * desc = 0;
	JSList * node;

	for (node = drivers; node; node = jack_slist_next (node)) {
		desc = (jack_driver_desc_t*)node->data;

		if (strcmp (desc->name, name) != 0) {
			desc = NULL;
		} else {
			break;
		}
	}

	return desc;
}

static void
jack_cleanup_files (const char *server_name)
{
	DIR *dir;
	struct dirent *dirent;
	char dir_name[PATH_MAX + 1] = "";

	jack_server_dir (server_name, dir_name);

	/* On termination, we remove all files that jackd creates so
	 * subsequent attempts to start jackd will not believe that an
	 * instance is already running.  If the server crashes or is
	 * terminated with SIGKILL, this is not possible.  So, cleanup
	 * is also attempted when jackd starts.
	 *
	 * There are several tricky issues.  First, the previous JACK
	 * server may have run for a different user ID, so its files
	 * may be inaccessible.  This is handled by using a separate
	 * JACK_TMP_DIR subdirectory for each user.  Second, there may
	 * be other servers running with different names.  Each gets
	 * its own subdirectory within the per-user directory.  The
	 * current process has already registered as `server_name', so
	 * we know there is no other server actively using that name.
	 */

	/* nothing to do if the server directory does not exist */
	if ((dir = opendir (dir_name)) == NULL) {
		return;
	}

	/* unlink all the files in this directory, they are mine */
	while ((dirent = readdir (dir)) != NULL) {

		char fullpath[PATH_MAX + 1];

		if ((strcmp (dirent->d_name, ".") == 0)
		    || (strcmp (dirent->d_name, "..") == 0)) {
			continue;
		}

		snprintf (fullpath, sizeof(fullpath), "%s/%s",
			  dir_name, dirent->d_name);

		if (unlink (fullpath)) {
			jack_error ("cannot unlink `%s' (%s)", fullpath,
				    strerror (errno));
		}
	}

	closedir (dir);

	/* now, delete the per-server subdirectory, itself */
	if (rmdir (dir_name)) {
		jack_error ("cannot remove `%s' (%s)", dir_name,
			    strerror (errno));
	}

	/* finally, delete the per-user subdirectory, if empty */
	if (rmdir (jack_user_dir ())) {
		if (errno != ENOTEMPTY) {
			jack_error ("cannot remove `%s' (%s)",
				    jack_user_dir (), strerror (errno));
		}
	}
}

static void
maybe_use_capabilities ()
{
#ifdef USE_CAPABILITIES
	int status;

	/* check to see if there is a pipe in the right descriptor */
	if ((status = fstat (PIPE_WRITE_FD, &pipe_stat)) == 0 &&
	    S_ISFIFO (pipe_stat.st_mode)) {

		/* tell jackstart we are up and running */
		char c = 1;

		if (write (PIPE_WRITE_FD, &c, 1) != 1) {
			jack_error ("cannot write to jackstart sync "
				    "pipe %d (%s)", PIPE_WRITE_FD,
				    strerror (errno));
		}

		if (close (PIPE_WRITE_FD) != 0) {
			jack_error ("jackd: error on startup pipe close: %s",
				    strerror (errno));
		} else {
			/* wait for jackstart process to set our capabilities */
			if (wait (&status) == -1) {
				jack_error ("jackd: wait for startup "
					    "process exit failed");
			}
			if (!WIFEXITED (status) || WEXITSTATUS (status)) {
				jack_error ("jackd: jackstart did not "
					    "exit cleanly");
				exit (1);
			}
		}
	}
#endif  /* USE_CAPABILITIES */
}

int
main (int argc, char *argv[])

{
	jack_driver_desc_t * desc;
	int replace_registry = 0;
	int do_sanity_checks = 1;
	int show_version = 0;

#ifdef HAVE_ZITA_BRIDGE_DEPS
	const char *options = "A:d:P:uvshVrRZTFlI:t:mM:n:Np:c:X:C:";
#else
	const char *options = "d:P:uvshVrRZTFlI:t:mM:n:Np:c:X:C:";
#endif
	struct option long_options[] =
	{
		/* keep ordered by single-letter option code */

#ifdef HAVE_ZITA_BRIDGE_DEPS
		{ "alsa-add",	       1, 0,		     'A' },
#endif
		{ "clock-source",      1, 0,		     'c' },
		{ "driver",	       1, 0,		     'd' },
		{ "help",	       0, 0,		     'h' },
		{ "tmpdir-location",   0, 0,		     'l' },
		{ "internal-client",   0, 0,		     'I' },
		{ "no-mlock",	       0, 0,		     'm' },
		{ "midi-bufsize",      1, 0,		     'M' },
		{ "name",	       1, 0,		     'n' },
		{ "no-sanity-checks",  0, 0,		     'N' },
		{ "port-max",	       1, 0,		     'p' },
		{ "realtime-priority", 1, 0,		     'P' },
		{ "no-realtime",       0, 0,		     'r' },
		{ "realtime",	       0, 0,		     'R' },
		{ "replace-registry",  0, &replace_registry, 0	 },
		{ "silent",	       0, 0,		     's' },
		{ "sync",	       0, 0,		     'S' },
		{ "timeout",	       1, 0,		     't' },
		{ "temporary",	       0, 0,		     'T' },
		{ "unlock",	       0, 0,		     'u' },
		{ "version",	       0, 0,		     'V' },
		{ "verbose",	       0, 0,		     'v' },
		{ "slave-driver",      1, 0,		     'X' },
		{ "nozombies",	       0, 0,		     'Z' },
		{ "timeout-thres",     2, 0,		     'C' },
		{ 0,		       0, 0,		     0	 }
	};
	int opt = 0;
	int option_index = 0;
	int seen_driver = 0;
	char *driver_name = NULL;
	char **driver_args = NULL;
	JSList * driver_params;
	JSList * slave_drivers = NULL;
	JSList * load_list = NULL;
	size_t midi_buffer_size = 0;
	int driver_nargs = 1;
	int i;
	int rc;
#ifdef HAVE_ZITA_BRIDGE_DEPS
	const char* alsa_add_client_name_playback = "zalsa_out";
	const char* alsa_add_client_name_capture = "zalsa_in";
	char alsa_add_args[64];
	char* dirstr;
#endif
	setvbuf (stdout, NULL, _IOLBF, 0);

	maybe_use_capabilities ();

	opterr = 0;
	while (!seen_driver &&
	       (opt = getopt_long (argc, argv, options,
				   long_options, &option_index)) != EOF) {
		switch (opt) {

#ifdef HAVE_ZITA_BRIDGE_DEPS
		case 'A':
			/* add a new internal client named after the ALSA device name
			   given as optarg, using the last character 'p' or 'c' to
			   indicate playback or capture. If there isn't one,
			   assume capture (common case: USB mics etc.)
			 */
			if ((dirstr = strstr (optarg, "%p")) != NULL && dirstr == (optarg + strlen (optarg) - 2)) {
				snprintf (alsa_add_args, sizeof(alsa_add_args), "%.*s_play:%s/-dhw:%.*s",
					  (int)strlen (optarg) - 2, optarg,
					  alsa_add_client_name_playback,
					  (int)strlen (optarg) - 2, optarg);
				load_list = jack_slist_append (load_list, strdup (alsa_add_args));
			} else if ((dirstr = strstr (optarg, "%c")) != NULL && dirstr == (optarg + strlen (optarg) - 2)) {
				snprintf (alsa_add_args, sizeof(alsa_add_args), "%.*s_rec:%s/-dhw:%.*s",
					  (int)strlen (optarg) - 2, optarg,
					  alsa_add_client_name_capture,
					  (int)strlen (optarg) - 2, optarg);
				load_list = jack_slist_append (load_list, strdup (alsa_add_args));
			} else {
				snprintf (alsa_add_args, sizeof(alsa_add_args), "%s_play:%s/-dhw:%s",
					  optarg,
					  alsa_add_client_name_playback,
					  optarg);
				load_list = jack_slist_append (load_list, strdup (alsa_add_args));
				snprintf (alsa_add_args, sizeof(alsa_add_args), "%s_rec:%s/-dhw:%s",
					  optarg,
					  alsa_add_client_name_capture,
					  optarg);
				load_list = jack_slist_append (load_list, strdup (alsa_add_args));
			}
			break;
#endif

		case 'c':
			if (tolower (optarg[0]) == 'h') {
				clock_source = JACK_TIMER_HPET;
			} else if (tolower (optarg[0]) == 'c') {
				/* For backwards compatibility with scripts,
				 * allow the user to request the cycle clock
				 * on the command line, but use the system
				 * clock instead
				 */
				clock_source = JACK_TIMER_SYSTEM_CLOCK;
			} else if (tolower (optarg[0]) == 's') {
				clock_source = JACK_TIMER_SYSTEM_CLOCK;
			} else {
				usage (stderr);
				return -1;
			}
			break;

		case 'C':
			if (optarg) {
				timeout_count_threshold = atoi (optarg);
			} else {
				timeout_count_threshold = 250;
			}
			break;

		case 'd':
			seen_driver = optind + 1;
			driver_name = optarg;
			break;

		case 'D':
			frame_time_offset = JACK_MAX_FRAMES - atoi (optarg);
			break;

		case 'l':
			/* special flag to allow libjack to determine jackd's idea of where tmpdir is */
			printf("%s\n", DEFAULT_TMP_DIR);

			exit (0);

		case 'I':
			load_list = jack_slist_append (load_list, optarg);
			break;

		case 'm':
			do_mlock = 0;
			break;

		case 'M':
			midi_buffer_size = (unsigned int)atol (optarg);
			break;

		case 'n':
			server_name = optarg;
			break;

		case 'N':
			do_sanity_checks = 0;
			break;

		case 'p':
			port_max = (unsigned int)atol (optarg);
			break;

		case 'P':
			realtime_priority = atoi (optarg);
			break;

		case 'r':
			realtime = 0;
			break;

		case 'R':
			/* this is now the default */
			realtime = 1;
			break;

		case 's':
			jack_set_error_function (silent_jack_error_callback);
			break;

		case 'S':
			/* this option is for jack2 only (synchronous mode) */
			break;

		case 'T':
			temporary = 1;
			break;

		case 't':
			client_timeout = atoi (optarg);
			break;

		case 'u':
			do_unlock = 1;
			break;

		case 'v':
			verbose = 1;
			break;

		case 'V':
			show_version = 1;
			break;

		case 'X':
			slave_drivers = jack_slist_append (slave_drivers, optarg);
			break;
		case 'Z':
			nozombies = 1;
			break;

		default:
			jack_error ("Unknown option character %c",
				    optopt);
		/*fallthru*/
		case 'h':
			usage (stdout);
			return -1;
		}
	}

	if (show_version) {
		printf ( "jackd version " VERSION
			 " tmpdir " DEFAULT_TMP_DIR
			 " protocol " PROTOCOL_VERSION
			 "\n");
		return 0;
	}

	copyright (stdout);

	if (do_sanity_checks && (0 < sanitycheck (realtime, FALSE))) {
		return -1;
	}

	if (!seen_driver) {
		usage (stderr);
		exit (1);
	}

	/* DIRTY HACK needed to pick up -X supplied as part of ALSA driver args. This is legacy
	   hack to make control apps like qjackctl based on the < 0.124 command line interface
	   continue to work correctly.

	   If -X seq was given as part of the driver args, load the ALSA MIDI slave driver.
	 */

	for (i = seen_driver; i < argc; ++i) {
		if (strcmp (argv[i], "-X") == 0) {
			if (argc >= i + 2) {
				if (strcmp (argv[i + 1], "seq") == 0) {
					slave_drivers = jack_slist_append (slave_drivers, "alsa_midi");
				}
			}
			break;
		} else if (strcmp (argv[i], "-Xseq") == 0) {
			slave_drivers = jack_slist_append (slave_drivers, "alsa_midi");
			break;
		}
	}

	drivers = jack_drivers_load ();

	if (!drivers) {
		fprintf (stderr, "jackd: no drivers found; exiting\n");
		exit (1);
	}

	if (midi_buffer_size != 0) {
		jack_port_type_info_t* port_type = &jack_builtin_port_types[JACK_MIDI_PORT_TYPE];
		port_type->buffer_size = midi_buffer_size * jack_midi_internal_event_size ();
		port_type->buffer_scale_factor = -1;
		if (verbose) {
			fprintf (stderr, "Set MIDI buffer size to %u bytes\n", port_type->buffer_size);
		}
	}

	desc = jack_find_driver_descriptor (driver_name);
	if (!desc) {
		fprintf (stderr, "jackd: unknown driver '%s'\n", driver_name);
		exit (1);
	}

	if (optind < argc) {
		driver_nargs = 1 + argc - optind;
	} else {
		driver_nargs = 1;
	}

	if (driver_nargs == 0) {
		fprintf (stderr, "No driver specified ... hmm. JACK won't do"
			 " anything when run like this.\n");
		return -1;
	}

	driver_args = (char**)malloc (sizeof(char *) * driver_nargs);
	driver_args[0] = driver_name;

	for (i = 1; i < driver_nargs; i++)
		driver_args[i] = argv[optind++];

	if (jack_parse_driver_params (desc, driver_nargs,
				      driver_args, &driver_params)) {
		exit (0);
	}

	if (server_name == NULL) {
		server_name = jack_default_server_name ();
	}

	rc = jack_register_server (server_name, replace_registry);
	switch (rc) {
	case EEXIST:
		fprintf (stderr, "`%s' server already active\n", server_name);
		exit (1);
	case ENOSPC:
		fprintf (stderr, "too many servers already active\n");
		exit (2);
	case ENOMEM:
		fprintf (stderr, "no access to shm registry\n");
		exit (3);
	default:
		if (verbose) {
			fprintf (stderr, "server `%s' registered\n",
				 server_name);
		}
	}

	/* clean up shared memory and files from any previous
	 * instance of this server name */
	jack_cleanup_shm ();
	jack_cleanup_files (server_name);

	/* run the server engine until it terminates */
	jack_main (desc, driver_params, slave_drivers, load_list);

	/* clean up shared memory and files from this server instance */
	if (verbose) {
		fprintf (stderr, "cleaning up shared memory\n");
	}
	jack_cleanup_shm ();
	if (verbose) {
		fprintf (stderr, "cleaning up files\n");
	}
	jack_cleanup_files (server_name);
	if (verbose) {
		fprintf (stderr, "unregistering server `%s'\n", server_name);
	}
	jack_unregister_server (server_name);

	exit (0);
}
