#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>

#include <time.h>

#include <config.h>

#include <jack/jack.h>

char * my_name;
jack_client_t *client;
unsigned int wait_timeout = 1000;

void
show_version (void)
{
	fprintf (stderr, "%s: JACK Audio Connection Kit version " VERSION "\n",
		my_name);
}

void
show_usage (void)
{
	show_version ();
	fprintf (stderr, "\nUsage: %s [options]\n", my_name);
	fprintf (stderr, "this is a test client, which just sleeps in its process_cb to simulate cpu load\n");
	fprintf (stderr, "options:\n");
	fprintf (stderr, "        -t, --timeout         Wait timeout in seconds\n");
	fprintf (stderr, "        -h, --help            Display this help message\n");
	fprintf (stderr, "        --version             Output version information and exit\n\n");
	fprintf (stderr, "For more information see http://jackaudio.org/\n");
}

void jack_shutdown(void *arg)
{
	fprintf(stderr, "JACK shut down, exiting ...\n");
	exit(1);
}

void signal_handler(int sig)
{
	jack_client_close(client);
	fprintf(stderr, "signal received, exiting ...\n");
	exit(0);
}

int
process_cb (jack_nframes_t nframes, void *arg)
{
	jack_time_t now  = jack_get_time();
	jack_time_t wait = now + wait_timeout; 

	while (jack_get_time() < wait) ;

	return 0;      
}

int
main (int argc, char *argv[])
{
	int c;
	int option_index;
	
	struct option long_options[] = {
		{ "timeout", 1, 0, 't' },
		{ "help", 0, 0, 'h' },
		{ "version", 0, 0, 'v' },
		{ 0, 0, 0, 0 }
	};

	my_name = strrchr(argv[0], '/');
	if (my_name == 0) {
		my_name = argv[0];
	} else {
		my_name ++;
	}

	while ((c = getopt_long (argc, argv, "t:hv", long_options, &option_index)) >= 0) {
		switch (c) {
		case 't':
			wait_timeout = atoi(optarg);
			break;
		case 'h':
			show_usage ();
			return 1;
			break;
		case 'v':
			show_version ();
			return 1;
			break;
		default:
			show_usage ();
			return 1;
			break;
		}
	}

	/* try to open server in a loop. breaking under certein conditions */

	client = jack_client_open( "load_test", JackNullOption, NULL );

	signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);

	jack_on_shutdown(client, jack_shutdown, 0);

	jack_set_process_callback( client, process_cb, NULL );

	jack_activate (client);

	sleep( -1 );

	exit (0);
}
