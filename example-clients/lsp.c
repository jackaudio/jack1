#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <jack/jack.h>

int
main (int argc, char *argv[])
{
	jack_client_t *client;
	const char **ports, **connections;
	unsigned int i, j;
	int show_con = 0;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-c")) {
			show_con = 1;
		}
	}

	/* try to become a client of the JACK server */

	if ((client = jack_client_new ("lsp")) == 0) {
		fprintf (stderr, "jack server not running?\n");
		return 1;
	}

	ports = jack_get_ports (client, NULL, NULL, 0);

	for (i = 0; ports[i]; ++i) {
		printf ("%s\n", ports[i]);
		if (show_con) {
			if ((connections = jack_port_get_all_connections (client, jack_port_by_name(client, ports[i]))) != 0) {
				for (j = 0; connections[j]; j++) {
					printf ("   %s\n", connections[j]);
				}
				free (connections);
			}
		}
	}
	
	jack_client_close (client);
	exit (0);
}
