#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <jack/jack.h>
#include <jack/port.h>
#include <jack/cycles.h>

jack_port_t *input_port;
jack_port_t *output_port;

volatile char *buf;
unsigned long stompsize;
int do_stomp = 0;

pthread_mutex_t foolock = PTHREAD_MUTEX_INITIALIZER;

int
process (jack_nframes_t nframes, void *arg)

{
	unsigned long i;
	unsigned long long now, then;

	jack_default_audio_sample_t *out = (jack_default_audio_sample_t *) jack_port_get_buffer (output_port, nframes);
	jack_default_audio_sample_t *in = (jack_default_audio_sample_t *) jack_port_get_buffer (input_port, nframes);

	now = get_cycles ();
	if (pthread_mutex_trylock (&foolock) == 0) {
		then = get_cycles ();
		if (do_stomp) {
			for (i = 0; i < stompsize; ++i) {
				buf[i]++;
			}
		} else {
			for (i = 0; i < stompsize; ++i) {
				buf[0]++;
			}
		}
		then = get_cycles ();
		pthread_mutex_unlock (&foolock);
	} 

	memcpy (out, in, sizeof (jack_default_audio_sample_t) * nframes);
	return 0;      
}

int
bufsize (jack_nframes_t nframes, void *arg)

{
	printf ("the maximum buffer size is now %lu\n", nframes);
	return 0;
}

int
srate (jack_nframes_t nframes, void *arg)

{
	printf ("the sample rate is now %lu/sec\n", nframes);
	return 0;
}

void
jack_shutdown (void *arg)
{
	printf ("shutdown by JACK\n");
	exit (1);
}

void *
other_thread (void *arg)
{
	while (1) {
		pthread_mutex_lock (&foolock);
		usleep (3000);
		pthread_mutex_unlock (&foolock);
		usleep (3000);
	}
	return 0;
}

int
main (int argc, char *argv[])

{
	jack_client_t *client;
	pthread_t other;

	if (argc < 2) {
		fprintf (stderr, "usage: jack_simple_client <name>\n");
		return 1;
	}

	/* try to become a client of the JACK server */

	if ((client = jack_client_new (argv[1])) == 0) {
		fprintf (stderr, "jack server not running?\n");
		return 1;
	}

	stompsize = atoi (argv[2]);
	buf = (char *) malloc (sizeof (char) * stompsize);

	do_stomp = atoi (argv[3]);

	pthread_create (&other, NULL, other_thread, NULL);

	/* tell the JACK server to call `process()' whenever
	   there is work to be done.
	*/

	jack_set_process_callback (client, process, 0);

	/* tell the JACK server to call `bufsize()' whenever
	   the maximum number of frames that will be passed
	   to `process()' changes
	*/

	jack_set_buffer_size_callback (client, bufsize, 0);

	/* tell the JACK server to call `srate()' whenever
	   the sample rate of the system changes.
	*/


	jack_set_sample_rate_callback (client, srate, 0);

	/* tell the JACK server to call `jack_shutdown()' if
	   it ever shuts down, either entirely, or if it
	   just decides to stop calling us.
	*/

	jack_on_shutdown (client, jack_shutdown, 0);

	/* display the current sample rate. once the client is activated 
	   (see below), you should rely on your own sample rate
	   callback (see above) for this value.
	*/

	printf ("engine sample rate: %lu\n", jack_get_sample_rate (client));

	/* create two ports */

	input_port = jack_port_register (client, "input", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
	output_port = jack_port_register (client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

	/* tell the JACK server that we are ready to roll */

	if (jack_activate (client)) {
		fprintf (stderr, "cannot activate client");
		return 1;
	}

	/* connect the ports. Note: you can't do this before
	   the client is activated (this may change in the future).
	*/

	if (jack_connect (client, "alsa_pcm:capture_1", jack_port_name (input_port))) {
		fprintf (stderr, "cannot connect input ports\n");
	}
	
	if (jack_connect (client, jack_port_name (output_port), "alsa_pcm:playback_1")) {
		fprintf (stderr, "cannot connect output ports\n");
	}

	/* Since this is just a toy, run for a few seconds, then finish */

	sleep (10);
	jack_client_close (client);
	printf ("finished OK\n");
	exit (0);
}

