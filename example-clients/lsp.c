#include <stdio.h>
#include <unistd.h>

#include <jack/jack.h>

int
main (int argc, char *argv[])

{
	jack_client_t *client;
	const char **ports;
	int i;

	/* try to become a client of the JACK server */

	if ((client = jack_client_new ("lsp")) == 0) {
		fprintf (stderr, "jack server not running?\n");
		return 1;
	}

	ports = jack_get_ports (client, NULL, NULL, 0);

	for (i = 0; ports[i]; ++i) {
		printf ("%s\n", ports[i]);
	}
	
	jack_client_close (client);
	exit (0);
}

