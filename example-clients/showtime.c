#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

#include <jack/jack.h>
#include <jack/transport.h>

typedef struct {
    volatile jack_nframes_t guard1;
    volatile jack_transport_info_t info;
    volatile jack_nframes_t guard2;
} guarded_transport_info_t;

guarded_transport_info_t now;
jack_client_t *client;


void
showtime ()
{
	guarded_transport_info_t current;
	int tries = 0;

	/* Since "now" is updated from the process() thread every
	 * buffer period, we must copy it carefully to avoid getting
	 * an incoherent hash of multiple versions. */
	do {
		/* Throttle the busy wait if we don't get the a clean
		 * copy very quickly. */
		if (tries > 10) {
			usleep (20);
			tries = 0;
		}
		current = now;
		tries++;

	} while (current.guard1 != current.guard2);

	if (current.info.valid & JackTransportPosition)
		printf ("frame: %lu ", current.info.frame);
	else
		printf ("frame: [-] ");

	if (current.info.valid & JackTransportState)
		printf ("state: %d ", current.info.transport_state);
	else
		printf ("state: [-] ");

	if (current.info.valid & JackTransportLoop)
		printf ("loop: %lu-%lu ", current.info.loop_start,
			current.info.loop_end);
	else
		printf ("loop: [-] ");

	if (current.info.valid & JackTransportBBT)
		printf ("BBT: %d|%d|%d\n", current.info.bar,
			current.info.beat, current.info.tick);
	else
		printf ("BBT: [-]\n");
}

int
process (jack_nframes_t nframes, void *arg)
{
	/* The guard flags contain a running counter of sufficiently
	 * high resolution, that showtime() can detect whether the
	 * last update is complete. */
	now.guard1 = jack_frame_time(client);
	jack_get_transport_info (client, &now.info);
	now.guard2 = now.guard1;

	return 0;      
}

void
jack_shutdown (void *arg)
{
	exit (1);
}

void
signal_handler (int sig)
{
	jack_client_close (client);
	fprintf (stderr, "signal received, exiting ...\n");
	exit (0);
}

int
main (int argc, char *argv[])

{
	/* try to become a client of the JACK server */

	if ((client = jack_client_new ("showtime")) == 0) {
		fprintf (stderr, "jack server not running?\n");
		return 1;
	}

	signal (SIGQUIT, signal_handler);
	signal (SIGTERM, signal_handler);
	signal (SIGHUP, signal_handler);
	signal (SIGINT, signal_handler);

	/* tell the JACK server to call `process()' whenever
	   there is work to be done.
	*/

	jack_set_process_callback (client, process, 0);

	/* tell the JACK server to call `jack_shutdown()' if
	   it ever shuts down, either entirely, or if it
	   just decides to stop calling us.
	*/

	jack_on_shutdown (client, jack_shutdown, 0);

	/* tell the JACK server that we are ready to roll */

	if (jack_activate (client)) {
		fprintf (stderr, "cannot activate client");
		return 1;
	}
	
	while (1) {
		usleep (100000);
		showtime ();
	}

	jack_client_close (client);
	exit (0);
}

