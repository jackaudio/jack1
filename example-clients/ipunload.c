#include <jack/jack.h>

int
main (int argc, char *argv[])
{
	jack_internal_client_close (argv[1]);
	return 0;
}
	
		
