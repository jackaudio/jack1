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
static char *alsa_pcm_name = "default";
static nframes_t frames_per_interrupt = 64;
static nframes_t srate = 48000;
static int realtime = 0;
static int realtime_priority = 10;
static int with_fork = 1;
static int hw_monitoring = 0;

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
	pid_t signal_pid = (pid_t) arg;
	jack_driver_t *driver;

	pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	if ((engine = jack_engine_new (realtime, realtime_priority)) == 0) {
		fprintf (stderr, "cannot create engine\n");
		kill (signal_pid, SIGTERM);
		return 0;
	}

	if ((driver = jack_driver_load (ADDON_DIR "/jack_alsa.so", 
					alsa_pcm_name, 
					frames_per_interrupt, 
					srate, 
					hw_monitoring)) == 0) {
		fprintf (stderr, "cannot load ALSA driver module\n");
		kill (signal_pid, SIGTERM);
		return 0;
	}

	jack_use_driver (engine, driver);

	if (jack_run (engine)) {
		fprintf (stderr, "cannot start main JACK thread\n");
		kill (signal_pid, SIGTERM);
		return 0;
	}

	jack_wait (engine);

	fprintf (stderr, "telling signal thread that the engine is done\n");
	kill (signal_pid, SIGHUP);

	return 0; /* nobody cares what this returns */
}

static void
jack_main ()
{
	int sig;
	int err;
	pthread_t waiter_thread;

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

	if (pthread_create (&waiter_thread, 0, jack_engine_waiter_thread, (void *) getpid())) {
		fprintf (stderr, "jackd: cannot create engine waiting thread\n");
		return;
	}

	/* Note: normal operation has with_fork == 1 */

	if (with_fork) {
		/* let the parent handle SIGINT */
		sigdelset (&signals, SIGINT);
	}

	err = sigwait (&signals, &sig);

	fprintf (stderr, "signal waiter: exiting due to signal %d\n", sig);

	pthread_cancel (waiter_thread);
	jack_engine_delete (engine);

	return;
}

static void usage () 

{
	fprintf (stderr, 
"usage: jackd [ --device OR -d ALSA-PCM-device ]
              [ --srate OR -r sample-rate ] 
              [ --frames-per-interrupt OR -p frames_per_interrupt ] 
              [ --realtime OR -R [ --realtime-priority OR -P priority ] ]
              [ --hw-monitor OR -h ]
              [ --spoon OR -F ]  (don't fork)
");
}	

int	       
main (int argc, char *argv[])

{
	const char *options = "hd:r:p:RP:FD:H";
	struct option long_options[] = 
	{ 
		{ "tmpdir", 1, 0, 'D' },
		{ "device", 1, 0, 'd' },
		{ "srate", 1, 0, 'r' },
		{ "frames-per-interrupt", 1, 0, 'p' },
		{ "help", 0, 0, 'h' },
		{ "realtime", 0, 0, 'R' },
		{ "realtime-priority", 1, 0, 'P' },
		{ "hw-monitor", 0, 0, 'H' },
		{ "spoon", 0, 0, 'F' },
		{ 0, 0, 0, 0 }
	};
	int option_index;
	int opt;
	
	opterr = 0;
	while ((opt = getopt_long (argc, argv, options, long_options, &option_index)) != EOF) {
		switch (opt) {
		case 'D':
			jack_set_temp_dir (optarg);
			break;

		case 'd':
			alsa_pcm_name = optarg;
			break;
			
		case 'r':
			srate = atoi (optarg);
			break;

		case 'p':
			frames_per_interrupt = atoi (optarg);
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

		case 'H':
			hw_monitoring = 1;
			break;

		case 'h':
		default:
			fprintf (stderr, "unknown option character %c\n", opt);
			usage();
			return -1;
		}
	}

	printf ( "jackd " VERSION "\n"
		 "Copyright 2001-2002 Paul Davis and others.\n\n"
		 "jackd comes with ABSOLUTELY NO WARRANTY\n"
		 "This is free software, and you are welcome to redistribute it\n"
		 "under certain conditions; see the file COPYING for details\n");

	if (!with_fork) {

		/* This is really here so that we can run gdb easily */

		jack_main ();

	} else {

		int pid = fork ();
		
		if (pid < 0) {

			fprintf (stderr, "could not fork jack server (%s)", strerror (errno));
			exit (1);

		} else if (pid == 0) {

			jack_main ();

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



