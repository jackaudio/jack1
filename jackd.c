#include <stdio.h>
#include <signal.h>
#include <getopt.h>

#include <jack/engine.h>
#include <jack/internal.h>
#include <jack/driver.h>

static sigset_t signals;

static jack_engine_t *engine;

static void *
signal_thread (void *arg)

{
	int sig;
	int err;

	pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	err = sigwait (&signals, &sig);
	fprintf (stderr, "exiting due to signal %d\n", sig);
	jack_engine_delete (engine);
	exit (err);

	/*NOTREACHED*/
	return 0;
}

int
catch_signals (void)

{
	pthread_t thread_id;

	sigemptyset (&signals);
	sigaddset(&signals, SIGHUP);
	sigaddset(&signals, SIGINT);
	sigaddset(&signals, SIGQUIT);
	sigaddset(&signals, SIGILL);
	sigaddset(&signals, SIGTRAP);
	sigaddset(&signals, SIGABRT);
	sigaddset(&signals, SIGIOT);
	sigaddset(&signals, SIGFPE);
	sigaddset(&signals, SIGKILL);
	sigaddset(&signals, SIGPIPE);
	sigaddset(&signals, SIGTERM);
	sigaddset(&signals, SIGCHLD);
	sigaddset(&signals, SIGCONT);
	sigaddset(&signals, SIGSTOP);
	sigaddset(&signals, SIGTSTP);
	sigaddset(&signals, SIGTTIN);
	sigaddset(&signals, SIGTTOU);

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

static char *alsa_pcm_name = "default";
static nframes_t frames_per_interrupt = 64;
static nframes_t srate = 48000;
static int realtime = 0;
static int realtime_priority = 10;

static void usage () 

{
	fprintf (stderr, "usage: engine [ -d ALSA PCM device ] [ -r sample-rate ] [ -p frames_per_interrupt ] [ -R [ -P priority ] ]\n");
}	

int	       
main (int argc, char *argv[])

{
	jack_driver_t *driver;
	const char *options = "hd:r:p:RP:";
	struct option long_options[] = 
	{ 
		{ "device", 1, 0, 'd' },
		{ "srate", 1, 0, 'r' },
		{ "frames-per-interrupt", 1, 0, 'p' },
		{ "help", 0, 0, 'h' },
		{ "realtime", 0, 0, 'R' },
		{ "realtime-priority", 1, 0, 'P' },
		{ 0, 0, 0, 0 }
	};
	int option_index;
	int opt;

	catch_signals ();

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

		case 'R':
			realtime = 1;
			break;

		case 'P':
			realtime_priority = atoi (optarg);
			break;

		case 'h':
		default:
			fprintf (stderr, "unknown option character %c\n", opt);
			usage();
			return -1;
		}
	}

	if ((engine = jack_engine_new (realtime, realtime_priority)) == 0) {
		fprintf (stderr, "cannot create engine\n");
		return 1;
	}

	if ((driver = jack_driver_load (ADDON_DIR "/jack_alsa.so", alsa_pcm_name, frames_per_interrupt, srate)) == 0) {
		fprintf (stderr, "cannot load ALSA driver module\n");
		return 1;
	}

	jack_use_driver (engine, driver);

	printf ("start engine ...\n");

	jack_run (engine);
	jack_wait (engine);

	return 0;
}



