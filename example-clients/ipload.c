#include <stdio.h>
#include <jack/jack.h>

int
main (int argc, char *argv[])
{
	char *name;
	char *so_name;
	char *so_data;

	if (argc < 3) {
		fprintf (stderr, "usage: %s client-name so-name [ so-data ]\n", argv[0]);
		return -1;
	}

	name = argv[1];
	so_name = argv[2];
	
	if (argc < 4) {
		so_data = "";
	} else {
		so_data = argv[3];
	}
	
	if (jack_internal_client_new (name, so_name, so_data) != 0) {
		fprintf (stderr, "could not load %s\n", so_name);
		return -1;
	} else {
		fprintf (stdout, "%s is running.\n", name);
		return 0;
	}
}
	
		
