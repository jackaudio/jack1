/*
    Copyright (C) 2001 Paul Davis
    
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
#include <string.h>
#include <errno.h>
#include <wait.h>

#include <jack/engine.h>
#include <jack/internal.h>
#include <jack/driver.h>

static sigset_t signals;
static jack_engine_t *engine = 0;
static int jackd_pid;
static int realtime = 0;
static int realtime_priority = 10;
static int with_fork = 1;
static int verbose = 0;
static int asio_mode = 0;

typedef struct {
    pid_t  pid;
    int argc;
    char **argv;
} waiter_arg_t;

static void
signal_handler (int sig)
{
	fprintf (stderr, "jackd: signal %d received\n", sig);
	kill (jackd_pid, SIGTERM);
}

static void
posix_me_harder (void)

{
	/* what's this for?

	   POSIX says that signals are delivered like this:

	   * if a thread has blocked that signal, it is not
	       a candidate to receive the signal.
           * of all threads not blocking the signal, pick
	       one at random, and deliver the signal.

           this means that a simple-minded multi-threaded
	   program can expect to get POSIX signals delivered
	   randomly to any one of its threads, 

	   here, we block all signals that we think we
	   might receive and want to catch. all "child"
	   threads will inherit this setting. if we
	   create a thread that calls sigwait() on the
	   same set of signals, implicitly unblocking
	   all those signals. any of those signals that
	   are delivered to the process will be delivered
	   to that thread, and that thread alone. this
	   makes cleanup for a signal-driven exit much
	   easier, since we know which thread is doing
	   it and more importantly, we are free to
	   call async-unsafe functions, because the
	   code is executing in normal thread context
	   after a return from sigwait().
	*/

	sigemptyset (&signals);
	sigaddset(&signals, SIGHUP);
	sigaddset(&signals, SIGINT);
	sigaddset(&signals, SIGQUIT);
	sigaddset(&signals, SIGILL);
	sigaddset(&signals, SIGTRAP);
	sigaddset(&signals, SIGABRT);
	sigaddset(&signals, SIGIOT);
	sigaddset(&signals, SIGFPE);
	sigaddset(&signals, SIGPIPE);
	sigaddset(&signals, SIGTERM);
	sigaddset(&signals, SIGUSR1);

	/* this can make debugging a pain, but it also makes
	   segv-exits cleanup_files after themselves rather than
	   leaving the audio thread active. i still
	   find it truly wierd that _exit() or whatever is done
	   by the default SIGSEGV handler does not
	   cancel all threads in a process, but what
	   else can we do?
	*/

	sigaddset(&signals, SIGSEGV);

	/* all child threads will inherit this mask */

	pthread_sigmask (SIG_BLOCK, &signals, 0);
}

static void *
jack_engine_waiter_thread (void *arg)
{
	waiter_arg_t *warg = (waiter_arg_t *) arg;
	jack_driver_t *driver;

	pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	if ((engine = jack_engine_new (realtime, realtime_priority, verbose)) == 0) {
		fprintf (stderr, "cannot create engine\n");
		kill (warg->pid, SIGTERM);
		return 0;
	}

	if (warg->argc) {
		
		if ((driver = jack_driver_load (warg->argc, warg->argv)) == 0) {
			fprintf (stderr, "cannot load driver module %s\n", warg->argv[0]);
			kill (warg->pid, SIGTERM);
			return 0;
		}

		jack_use_driver (engine, driver);
	}

	if (asio_mode) {
		jack_set_asio_mode (engine, TRUE);
	} 

	if (jack_run (engine)) {
		fprintf (stderr, "cannot start main JACK thread\n");
		kill (warg->pid, SIGTERM);
		return 0;
	}

	jack_wait (engine);

	fprintf (stderr, "telling signal thread that the engine is done\n");
	kill (warg->pid, SIGHUP);

	return 0; /* nobody cares what this returns */
}

static void
jack_main (int argc, char **argv)
{
	int sig;
	int err;
	pthread_t waiter_thread;
	waiter_arg_t warg;

	pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	posix_me_harder ();
	
	/* what we'd really like to do here is to be able to 
	   wait for either the engine to stop or a POSIX signal,
	   whichever arrives sooner. but there is no mechanism
	   to do that, so instead we create a thread to wait
	   for the engine to finish, and here we stop and wait
	   for any (reasonably likely) POSIX signal.

	   if the engine finishes first, the waiter thread will
	   tell us about it via a signal.

	   if a signal arrives, we'll stop the engine and then
	   exit. 

	   in normal operation, our parent process will be waiting
	   for us and will cleanup.
	*/

	warg.pid = getpid();
	warg.argc = argc;
	warg.argv = argv;

	if (pthread_create (&waiter_thread, 0, jack_engine_waiter_thread, &warg)) {
		fprintf (stderr, "jackd: cannot create engine waiting thread\n");
		return;
	}

	/* Note: normal operation has with_fork == 1 */

	if (with_fork) {
		/* let the parent handle SIGINT */
		sigdelset (&signals, SIGINT);
	}

	while(1) {
		err = sigwait (&signals, &sig);
		
		if (sig == SIGUSR1) {
			jack_dump_configuration(engine, 1);
		} else {
			/* continue to kill engine */
			break;
		}
	} 
	
	pthread_cancel (waiter_thread);
	jack_engine_delete (engine);

	return;
}

static void usage () 

{
	fprintf (stderr, "\
usage: jackd [ --asio OR -a ]
	     [ --realtime OR -R [ --realtime-priority OR -P priority ] ]
             [ --verbose OR -v ]
             [ --tmpdir OR -D directory-for-temporary-files ]
         -d driver [ ... driver args ... ]
");
}	

int	       
main (int argc, char *argv[])

{
	const char *options = "ad:D:P:vhRF";
	struct option long_options[] = 
	{ 
		{ "asio", 0, 0, 'a' },
		{ "driver", 1, 0, 'd' },
		{ "tmpdir", 1, 0, 'D' },
		{ "verbose", 0, 0, 'v' },
		{ "help", 0, 0, 'h' },
		{ "realtime", 0, 0, 'R' },
		{ "realtime-priority", 1, 0, 'P' },
		{ "spoon", 0, 0, 'F' },
		{ 0, 0, 0, 0 }
	};
	int option_index;
	int opt;
	int seen_driver = 0;
	char *driver_name = 0;
	char **driver_args = 0;
	int driver_nargs = 1;
	int i;

	opterr = 0;
	while (!seen_driver && (opt = getopt_long (argc, argv, options, long_options, &option_index)) != EOF) {
		switch (opt) {
		case 'a':
			asio_mode = TRUE;
			break;

		case 'D':
			jack_set_temp_dir (optarg);
			break;

		case 'd':
			seen_driver = 1;
			driver_name = optarg;
			break;

		case 'v':
			verbose = 1;
			break;

		case 'F':
			with_fork = 0;
			break;

		case 'P':
			realtime_priority = atoi (optarg);
			break;

		case 'R':
			realtime = 1;
			break;

		default:
			fprintf (stderr, "unknown option character %c\n", optopt);
			/*fallthru*/
		case 'h':
			usage();
			return -1;
		}
	}

	if (!seen_driver) {
		usage ();
		exit (1);
	}

	if (optind < argc) {
		driver_nargs = 1 + argc - optind;
	} else {
		driver_nargs = 1;
	}

	driver_args = (char **) malloc (sizeof (char *) * driver_nargs);
	driver_args[0] = driver_name;
	
	for (i = 1; i < driver_nargs; i++) {
		driver_args[i] = argv[optind++];
	}

	printf ( "jackd " VERSION "\n"
		 "Copyright 2001-2002 Paul Davis and others.\n"
		 "jackd comes with ABSOLUTELY NO WARRANTY\n"
		 "This is free software, and you are welcome to redistribute it\n"
		 "under certain conditions; see the file COPYING for details\n\n");

	if (!with_fork) {

		/* This is really here so that we can run gdb easily */

		jack_main (driver_nargs, driver_args);

	} else {

		int pid = fork ();
		
		if (pid < 0) {

			fprintf (stderr, "could not fork jack server (%s)", strerror (errno));
			exit (1);

		} else if (pid == 0) {

			jack_main (driver_nargs, driver_args);

		} else {
			jackd_pid = pid;

			signal (SIGHUP, signal_handler);
			signal (SIGINT, signal_handler);
			signal (SIGQUIT, signal_handler);
			signal (SIGTERM, signal_handler);

			waitpid (pid, NULL, 0);
		}
	}

	jack_cleanup_shm ();
	jack_cleanup_files ();

	return 0;
}
