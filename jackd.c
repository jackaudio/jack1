#include <stdio.h>
#include <signal.h>
#include <getopt.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <wait.h>

#include <jack/engine.h>
#include <jack/internal.h>
#include <jack/driver.h>

static sigset_t signals;
static jack_engine_t *engine;
static int jackd_pid;
static char *alsa_pcm_name = "default";
static nframes_t frames_per_interrupt = 64;
static nframes_t srate = 48000;
static int realtime = 0;
static int realtime_priority = 10;

static void
cleanup ()
{
	DIR *dir;
	struct dirent *dirent;

	/* its important that we remove all files that jackd creates
	   because otherwise subsequent attempts to start jackd will
	   believe that an instance is already running.
	*/

	if ((dir = opendir ("/tmp")) == NULL) {
		fprintf (stderr, "jackd(%li): cleanup - cannot open scratch directory (%s)\n", (long)getpid(), strerror (errno));
		return;
	}

	while ((dirent = readdir (dir)) != NULL) {
		if (strncmp (dirent->d_name, "jack-", 5) == 0) {
			unlink (dirent->d_name);
		}
	}

	closedir (dir);
}

static void
signal_handler (int sig)
{
	fprintf (stderr, "killing jackd at %d\n", jackd_pid);
	kill (jackd_pid, SIGTERM);
	exit (-sig);
}

static void
catch_signals (void)
{
	/* what's this for? 

	   this just makes sure that if we are using the fork
	   approach to cleanup (see main()), the waiting
	   process will catch common "interrupt" signals
	   and terminate the real server appropriately.
	*/

	signal (SIGHUP, signal_handler);
	signal (SIGINT, signal_handler);
	signal (SIGQUIT, signal_handler);
	signal (SIGTERM, signal_handler);
}

static void *
signal_thread (void *arg)

{
	int sig;
	int err;

	pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	err = sigwait (&signals, &sig);
	fprintf (stderr, "exiting due to signal %d\n", sig);
	jack_engine_delete (engine);
	cleanup ();
	exit (err);

	/*NOTREACHED*/
	return 0;
}

static int
posix_me_harder (void)

{
	pthread_t thread_id;

	/* what's this for?

	   POSIX says that signals are delivered like this:

	   * if a thread has blocked that signal, it is not
	       a candidate to receive the signal.
           * of all threads not blocking the signal, pick
	       one at random, and deliver the signal.

           this means that a simple-minded multi-threaded
	   program can expect to get POSIX signals delivered
	   to any of its threads.

	   here, we block all signals that we think we
	   might receive and want to catch. all later
	   threads will inherit this setting. then we
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
	   segv-exits cleanup after themselves rather than
	   leaving the audio thread active. i still
	   find it truly wierd that _exit() or whatever is done
	   by the default SIGSEGV handler does not
	   cancel all threads in a process, but what
	   else can we do?
	*/

	sigaddset(&signals, SIGSEGV);

	/* all child threads will inherit this mask */

	pthread_sigmask (SIG_BLOCK, &signals, 0);

	/* start a thread to wait for signals */

	if (pthread_create (&thread_id, 0, signal_thread, 0)) {
		fprintf (stderr, "cannot create signal catching thread");
		return -1;
	}

	pthread_detach (thread_id);

	return 0;
}

static void
jack_main ()
{
	jack_driver_t *driver;

	posix_me_harder ();

	if ((engine = jack_engine_new (realtime, realtime_priority)) == 0) {
		fprintf (stderr, "cannot create engine\n");
		return;
	}

	if ((driver = jack_driver_load (ADDON_DIR "/jack_alsa.so", alsa_pcm_name, frames_per_interrupt, srate)) == 0) {
		fprintf (stderr, "cannot load ALSA driver module\n");
		return;
	}

	jack_use_driver (engine, driver);
	jack_run (engine);
	jack_wait (engine);
}

static void usage () 

{
	fprintf (stderr, 
"usage: jackd [ --device OR -d ALSA-PCM-device ]
              [ --srate OR -r sample-rate ] 
              [ --frames-per-interrupt OR -p frames_per_interrupt ] 
              [ --realtime OR -R [ --realtime-priority OR -P priority ] ]
              [ --spoon OR -F ]  (don't fork)
");
}	

int	       
main (int argc, char *argv[])

{
	const char *options = "hd:r:p:RP:F";
	struct option long_options[] = 
	{ 
		{ "device", 1, 0, 'd' },
		{ "srate", 1, 0, 'r' },
		{ "frames-per-interrupt", 1, 0, 'p' },
		{ "help", 0, 0, 'h' },
		{ "realtime", 0, 0, 'R' },
		{ "realtime-priority", 1, 0, 'P' },
		{ "spoon", 0, 0, 'F' },
		{ 0, 0, 0, 0 }
	};
	int option_index;
	int opt;
	int no_fork = 0;
	
	opterr = 0;
	while ((opt = getopt_long (argc, argv, options, long_options, &option_index)) != EOF) {
		switch (opt) {
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
			no_fork = 1;
			break;

		case 'P':
			realtime_priority = atoi (optarg);
			break;

		case 'R':
			realtime = 1;
			break;

		case 'h':
		default:
			fprintf (stderr, "unknown option character %c\n", opt);
			usage();
			return -1;
		}
	}

	if (no_fork) {
		jack_main ();
		cleanup ();

	} else {

		int pid = fork ();
		
		if (pid < 0) {
			fprintf (stderr, "could not fork jack server (%s)", strerror (errno));
			exit (1);
		} else if (pid == 0) {
			jack_main ();
		} else {
			jackd_pid = pid;
			catch_signals ();
			waitpid (pid, NULL, 0);
			cleanup ();
		}
	}

	return 0;
}



