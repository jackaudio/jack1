#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

#include <jack/jack.h>
#include <jack/transport.h>

jack_client_t *client;
jack_transport_info_t now;

void
showtime ()
{
	jack_transport_info_t current = now;
	printf ("frame: %lu state: %d loop: %lu-%lu "
		"BBT: %d|%d|%d\n",
		current.frame, current.transport_state, current.loop_start, current.loop_end,
		current.bar,
		current.beat,
		current.tick);
}

int
process (jack_nframes_t nframes, void *arg)
{
	now.valid = JackTransportState|JackTransportPosition|JackTransportLoop;
	jack_get_transport_info (client, &now);
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
	fprintf (stderr, "signal received, exiting ...\n");
	jack_client_close (client);
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

