/* -*- mode: c; c-file-style: "bsd"; -*- */
/*
    Copyright (C) 2001-2003 Paul Davis
    
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

    $Id$
*/

#include <stdio.h>
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

#include <config.h>

#include <jack/engine.h>
#include <jack/internal.h>
#include <jack/driver.h>
#include <jack/shm.h>
#include <jack/driver_parse.h>

#ifdef USE_CAPABILITIES

#include <sys/stat.h>
/* capgetp and capsetp are linux only extensions, not posix */
#undef _POSIX_SOURCE
#include <sys/capability.h>
#include <jack/start.h>

static struct stat pipe_stat;

#endif

static JSList * drivers = NULL;
static sigset_t signals;
static jack_engine_t *engine = 0;
static int realtime = 0;
static int realtime_priority = 10;
static int do_mlock = 1;
static int temporary = 0;
static int verbose = 0;
static int client_timeout = 500; /* msecs */

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

static int
jack_main (jack_driver_desc_t * driver_desc, JSList * driver_params)
{
	int sig;
	int i;
	sigset_t allsignals;
	struct sigaction action;
	int waiting;

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
	sigaddset(&signals, SIGHUP);
	sigaddset(&signals, SIGINT);
	sigaddset(&signals, SIGQUIT);
	sigaddset(&signals, SIGPIPE);
	sigaddset(&signals, SIGTERM);
	sigaddset(&signals, SIGUSR1);
	sigaddset(&signals, SIGUSR2);

	/* all child threads will inherit this mask unless they
	 * explicitly reset it 
	 */

	pthread_sigmask (SIG_BLOCK, &signals, 0);
	
	/* get the engine/driver started */

	if ((engine = jack_engine_new (realtime, realtime_priority, do_mlock,
				       temporary, verbose, client_timeout,
				       getpid(), drivers)) == 0) {
		fprintf (stderr, "cannot create engine\n");
		return -1;
	}

	fprintf (stderr, "loading driver ..\n");
	
	if (jack_engine_load_driver (engine, driver_desc, driver_params)) {
		fprintf (stderr, "cannot load driver module %s\n", driver_desc->name);
		return -1;
	}

        if (engine->driver->start (engine->driver) != 0) {
                jack_error ("cannot start driver");
		return -1;
	}

	/* install a do-nothing handler because otherwise pthreads
	   behaviour is undefined when we enter sigwait.
	*/

	sigfillset (&allsignals);
	action.sa_handler = do_nothing_handler;
	action.sa_mask = allsignals;
	action.sa_flags = SA_RESTART|SA_RESETHAND;

	for (i = 1; i < NSIG; i++) {
		if (sigismember (&signals, i)) {
			sigaction (i, &action, 0);
		} 
	}
	
	if (verbose) {
		fprintf (stderr, "%d waiting for signals\n", getpid());
	}

	waiting = TRUE;

	while (waiting) {
		sigwait (&signals, &sig);

		fprintf (stderr, "jack main caught signal %d\n", sig);
		
		switch (sig) {
		case SIGUSR1:
			jack_dump_configuration(engine, 1);
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

	filename = malloc (strlen (ADDON_DIR) + 1 + strlen (sofile) + 1);
	sprintf (filename, "%s/%s", ADDON_DIR, sofile);

	if (verbose)
		printf ("getting driver descriptor from %s\n", filename);

	dlhandle = dlopen (filename, RTLD_NOW|RTLD_GLOBAL);
	if (!dlhandle) {
		jack_error ("could not open driver .so '%s': %s\n", filename, dlerror ());
		free (filename);
		return NULL;
	}

	dlerror ();

	so_get_descriptor = (JackDriverDescFunction)
		dlsym (dlhandle, "driver_get_descriptor");

	dlerr = dlerror ();
	if (dlerr) {
		dlclose (dlhandle);
		free (filename);
		return NULL;
	}

	descriptor = so_get_descriptor ();
	if (!descriptor) {
		jack_error ("driver from '%s' returned NULL descriptor\n", filename);
		dlclose (dlhandle);
		free (filename);
		return NULL;
	}

	err = dlclose (dlhandle);
	if (err) {
		jack_error ("error closing driver .so '%s': %s\n", filename, dlerror ());
	}


	/* check it doesn't exist already */
	for (node = drivers; node; node = jack_slist_next (node)) {
		other_descriptor = (jack_driver_desc_t *) node->data;

		if (strcmp (descriptor->name, other_descriptor->name) == 0) {
			jack_error ("the drivers in '%s' and '%s' both have the name '%s'; using the first\n",
				    other_descriptor->file, filename, other_descriptor->name);
			/* FIXME: delete the descriptor */
			free (filename);
			return NULL;
		}
	}

	strncpy (descriptor->file, filename, PATH_MAX);
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

	/* search through the ADDON_DIR and add get descriptors
	   from the .so files in it */
	dir_stream = opendir (ADDON_DIR);
	if (!dir_stream) {
		jack_error ("could not open driver directory %s: %s\n", ADDON_DIR, strerror (errno));
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
		jack_error ("error closing driver directory %s: %s\n", ADDON_DIR, strerror (errno));
	}

	if (!driver_list) {
		jack_error ("could not find any drivers in %s!\n", ADDON_DIR);
		return NULL;
	}

	return driver_list;
}

static void copyright (FILE* file)
{
	fprintf (file, "jackd " VERSION "\n"
"Copyright 2001-2003 Paul Davis and others.\n"
"jackd comes with ABSOLUTELY NO WARRANTY\n"
"This is free software, and you are welcome to redistribute it\n"
"under certain conditions; see the file COPYING for details\n\n");
}

static void usage (FILE *file) 
{
	copyright (file);
	fprintf (file, "\n"
"usage: jackd [ --realtime OR -R [ --realtime-priority OR -P priority ] ]\n"
"             [ --no-mlock OR -m ]\n"
"             [ --timeout OR -t client-timeout-in-msecs ]\n"
"             [ --verbose OR -v ]\n"
"             [ --silent OR -s ]\n"
"             [ --version OR -V ]\n"
"         -d driver [ ... driver args ... ]\n"
"             driver can be `alsa', `dummy', `oss' or `portaudio'\n\n"
"       jackd -d driver --help\n"
"             to display options for each driver\n\n");
}	

static jack_driver_desc_t *
jack_find_driver_descriptor (const char * name)
{
	jack_driver_desc_t * desc = 0;
	JSList * node;

	for (node = drivers; node; node = jack_slist_next (node)) {
		desc = (jack_driver_desc_t *) node->data;

		if (strcmp (desc->name, name) != 0) {
			desc = NULL;
		} else {
			break;
		}
	}

	return desc;
}

int	       
main (int argc, char *argv[])

{
    jack_driver_desc_t * desc;
	const char *options = "-ad:P:vshVRTFl:t:m";
	struct option long_options[] = 
	{ 
		{ "driver", 1, 0, 'd' },
		{ "verbose", 0, 0, 'v' },
		{ "help", 0, 0, 'h' },
		{ "no-mlock", 0, 0, 'm' },
		{ "realtime", 0, 0, 'R' },
		{ "realtime-priority", 1, 0, 'P' },
		{ "timeout", 1, 0, 't' },
		{ "temporary", 0, 0, 'T' },
		{ "version", 0, 0, 'V' },
		{ "silent", 0, 0, 's' },
		{ 0, 0, 0, 0 }
	};
	int opt = 0;
	int option_index = 0;
	int seen_driver = 0;
	char *driver_name = 0;
	char **driver_args = 0;
	JSList * driver_params;
	int driver_nargs = 1;
	int show_version = 0;
	int i;
#ifdef USE_CAPABILITIES
	int status;
#endif

	setvbuf (stdout, NULL, _IOLBF, 0);

#ifdef USE_CAPABILITIES

	/* check to see if there is a pipe in the right descriptor */
	if ((status = fstat (PIPE_WRITE_FD, &pipe_stat)) == 0 &&
	    S_ISFIFO(pipe_stat.st_mode)) {

		/* tell jackstart we are up and running */
  	        char c = 1;

	        if (write (PIPE_WRITE_FD, &c, 1) != 1) {
		        fprintf (stderr, "cannot write to jackstart sync "
				 "pipe %d (%s)\n", PIPE_WRITE_FD,
				 strerror (errno));
	        }

		if (close(PIPE_WRITE_FD) != 0) {
			fprintf(stderr,
				"jackd: error on startup pipe close: %s\n",
				strerror (errno));
		} else {
			/* wait for jackstart process to set our capabilities */
			if (wait (&status) == -1) {
				fprintf (stderr, "jackd: wait for startup "
					 "process exit failed\n");
			}
			if (!WIFEXITED (status) || WEXITSTATUS (status)) {
				fprintf(stderr, "jackd: jackstart did not "
					"exit cleanly\n");
				exit (1);
			}
		}
	}
#endif

	opterr = 0;
	while (!seen_driver &&
	       (opt = getopt_long (argc, argv, options,
				   long_options, &option_index)) != EOF) {
		switch (opt) {

		case 'd':
			seen_driver = 1;
			driver_name = optarg;
			break;

		case 'v':
			verbose = 1;
			break;

		case 's':
			jack_set_error_function (silent_jack_error_callback);
			break;

		case 'm':
			do_mlock = 0;
			break;

		case 'P':
			realtime_priority = atoi (optarg);
			break;

		case 'R':
			realtime = 1;
			break;

		case 'T':
			temporary = 1;
			break;

		case 't':
			client_timeout = atoi (optarg);
			break;

		case 'V':
			show_version = 1;
			break;

		default:
			fprintf (stderr, "unknown option character %c\n", optopt);
			/*fallthru*/
		case 'h':
			usage (stdout);
			return -1;
		}
	}

	if (show_version) {
		printf ( "jackd version " VERSION "\n");
#ifdef DEFAULT_TMP_DIR
		printf ( "default tmp directory: " DEFAULT_TMP_DIR "\n");
#else
		printf ( "default tmp directory: /tmp\n");
#endif
		return -1;
	}

	if (!seen_driver) {
		usage (stderr);
		exit (1);
	}

	drivers = jack_drivers_load ();
	if (!drivers)
	{
		fprintf (stderr, "jackd: no drivers found; exiting\n");
		exit (1);
	}

	desc = jack_find_driver_descriptor (driver_name);
	if (!desc)
	{
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

	driver_args = (char **) malloc (sizeof (char *) * driver_nargs);
	driver_args[0] = driver_name;
	
	for (i = 1; i < driver_nargs; i++) {
		driver_args[i] = argv[optind++];
	}

	i = jack_parse_driver_params (desc, driver_nargs, driver_args, &driver_params);
	if (i)
		exit (0);

	copyright (stdout);

	jack_cleanup_shm ();
	jack_cleanup_files ();

	jack_main (desc, driver_params);

	jack_cleanup_shm ();
	jack_cleanup_files ();

	exit (0);
}

