#include <stdio.h>
#include <unistd.h>

#include <jack/jack.h>

int
main (int argc, char *argv[])

{
	jack_client_t *client;

	if ((client = jack_client_new ("input monitoring")) == 0) {
		fprintf (stderr, "jack server not running?\n");
		return 1;
	}

	jack_port_request_monitor (client, "ALSA I/O:Input 1", TRUE);
	sleep (10);
	jack_port_request_monitor (client, "ALSA I/O:Input 1", FALSE);
	jack_client_close (client);
	exit (0);
}

