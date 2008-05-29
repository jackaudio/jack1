/*
    Copyright (C) 2002 Jeremy Hall
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>

#include <config.h>

#include <jack/jack.h>

jack_port_t *input_port;
jack_port_t *output_port;
int connecting, disconnecting;
#define TRUE 1
#define FALSE 0

void
show_version (char *my_name)
{
	fprintf (stderr, "%s: JACK Audio Connection Kit version " VERSION "\n", my_name);
}

void
show_usage (char *my_name)
{
	show_version (my_name);
	fprintf (stderr, "\nusage: %s [options] <src_port> <dst_port>\n", my_name);
	fprintf (stderr, "Connects two JACK ports together.\n\n");
	fprintf (stderr, "        The source port must be an output port of the source client.\n");
	fprintf (stderr, "        The destination port must be an input port of the destination client.\n");
	fprintf (stderr, "        -s, --server <name>   Connect to the jack server named <name>\n");
	fprintf (stderr, "        -v, --version         Output version information and exit\n");
	fprintf (stderr, "        -h, --help            Display this help message\n\n");
	fprintf (stderr, "For more information see http://jackaudio.org/\n");
}

int
main (int argc, char *argv[])
{
	jack_client_t *client;
	jack_status_t status;
	char *server_name = NULL;
    int c;
    int option_index;
    jack_options_t options = JackNoStartServer;
	char *my_name = strrchr(argv[0], '/');

	struct option long_options[] = {
	    { "server", 1, 0, 's' },
		{ "help", 0, 0, 'h' },
		{ "version", 0, 0, 'v' },
		{ 0, 0, 0, 0 }
	};

	while ((c = getopt_long (argc, argv, "s:AclLphvt", long_options, &option_index)) >= 0) {
		switch (c) {
		case 's':
            server_name = (char *) malloc (sizeof (char) * strlen(optarg));
            strcpy (server_name, optarg);
            options |= JackServerName;
            break;
		case 'h':
			show_usage (my_name);
			return 1;
			break;
		case 'v':
			show_version (my_name);
			return 1;
			break;
		default:
			show_usage (my_name);
			return 1;
			break;
		}
	}

	connecting = disconnecting = FALSE;
	if (my_name == 0) {
		my_name = argv[0];
	} else {
	my_name ++;
	}

	if (strstr(my_name, "disconnect")) {
		disconnecting = TRUE;
	} else
	if (strstr(my_name, "connect")) {
		connecting = TRUE;
	} else {
		fprintf(stderr, "ERROR! client should be called jack_connect or jack_disconnect. client is called %s\n", my_name);
		return 1;
	}

	if (argc < 3) show_usage(my_name);

	/* try to become a client of the JACK server */

	if ((client = jack_client_open (my_name, options, &status, server_name)) == 0) {
		fprintf (stderr, "jack server not running?\n");
		return 1;
	}

	/* display the current sample rate. once the client is activated 
	   (see below), you should rely on your own sample rate
	   callback (see above) for this value.
	*/

	printf ("engine sample rate: %" PRIu32 "\n",
		jack_get_sample_rate (client));

	/* find the two ports */

	if ((input_port = jack_port_by_name(client, argv[argc-1])) == 0) {
		fprintf (stderr, "ERROR %s not a valid port\n", argv[argc-1]);
		return 1;
		}
	if ((output_port = jack_port_by_name(client, argv[argc-2])) == 0) {
		fprintf (stderr, "ERROR %s not a valid port\n", argv[argc-2]);
		return 1;
		}

	/* tell the JACK server that we are ready to roll */

	if (jack_activate (client)) {
		fprintf (stderr, "cannot activate client");
		return 1;
	}

	/* connect the ports. Note: you can't do this before
	   the client is activated (this may change in the future).
	*/

/* jack_port_connect not implemented
	if (jack_port_connect(client, input_port, output_port)) {
		fprintf (stderr, "cannot connect ports\n");
	}
*/
	if (connecting) {
		if (jack_connect(client, jack_port_name(input_port), jack_port_name(output_port))) {
			fprintf (stderr, "cannot connect ports\n");
			return 1;
	}
	}
	if (disconnecting) {
		if (jack_disconnect(client, jack_port_name(input_port), jack_port_name(output_port))) {
			fprintf (stderr, "cannot disconnect ports\n");
			return 1;
	}
	}

	jack_client_close (client);
	exit (0);
}

