#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <jack/jack.h>

#define TRUE 1
#define FALSE 0

int
main (int argc, char *argv[])

{
	jack_client_t *client;

	if ((client = jack_client_new ("input monitoring")) == 0) {
		fprintf (stderr, "jack server not running?\n");
		return 1;
	}

	if (jack_port_request_monitor_by_name (client, argv[1], TRUE)) {
		fprintf (stderr, "could not enable monitoring for %s\n", argv[1]);
	}
	sleep (30);
	if (jack_port_request_monitor_by_name (client, argv[1], FALSE)) {
		fprintf (stderr, "could not disable monitoring for %s\n", argv[1]);
	}
	jack_client_close (client);
	exit (0);
}

