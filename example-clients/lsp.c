#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>

#include <jack/jack.h>

int
main (int argc, char *argv[])
{
	jack_client_t *client;
	const char **ports, **connections;
	unsigned int i, j;
	int show_con = 0;
	int show_latency = 0;
	int show_properties = 0;
	int c;
	int option_index;

	struct option long_options[] = {
		{ "connections", 0, 0, 'c' },
		{ "latency", 0, 0, 'l' },
		{ "properties", 0, 0, 'p' },
		{ 0, 0, 0, 0 }
	};

	while ((c = getopt_long (argc, argv, "clp", long_options, &option_index)) >= 0) {
		switch (c) {
		case 'c':
			show_con = 1;
			break;
		case 'l':
			show_latency = 1;
			break;
		case 'p':
			show_properties = 1;
			break;
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
		if (show_latency) {
			jack_port_t *port = jack_port_by_name (client, ports[i]);
			if (port) {
				printf ("	latency = %lu frames\n", jack_port_get_total_latency (client, port));
				free (port);
			}
		}
		if (show_properties) {
			jack_port_t *port = jack_port_by_name (client, ports[i]);
			if (port) {
				int flags = jack_port_flags (port);
				printf ("	properties: ");
				if (flags & JackPortIsInput) {
					fputs ("input,", stdout);
				}
				if (flags & JackPortIsOutput) {
					fputs ("output,", stdout);
				}
				if (flags & JackPortCanMonitor) {
					fputs ("can-monitor,", stdout);
				}
				if (flags & JackPortIsPhysical) {
					fputs ("physical,", stdout);
				}
				if (flags & JackPortIsTerminal) {
					fputs ("terminal,", stdout);
				}
				putc ('\n', stdout);
			}
		}
	}
	jack_client_close (client);
	exit (0);
}
