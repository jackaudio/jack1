/*
    Copyright (C) 2003 Robert Ham <rah@bash.sh>
    Copyright (C) 2001 Paul Davis

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

    $Id$
*/

#include <math.h>
#include <stdio.h>
#include <memory.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <getopt.h>

#include <jack/types.h>
#include <jack/internal.h>
#include <jack/engine.h>
#include <jack/time.h>

#include "dummy_driver.h"

#undef DEBUG_WAKEUP


static int
dummy_driver_audio_start (dummy_driver_t *driver)
{
  return 0;
}

static int
dummy_driver_audio_stop (dummy_driver_t *driver)
{
  return 0;
}

static jack_nframes_t 
dummy_driver_wait (dummy_driver_t *driver, int extra_fd, int *status,
		   float *delayed_usecs)
{
  jack_time_t starting_time = jack_get_microseconds();
  jack_time_t processing_time = (driver->last_wait_ust?
				 starting_time - driver->last_wait_ust: 0);
  jack_time_t sleeping_time = driver->wait_time - processing_time;

  /* JOQ: usleep() is inaccurate for small buffer sizes with Linux
   * 2.4.  I suspect it can't wait for less than one (or maybe even
   * two) scheduler timeslices.  Linux 2.6 is probably better. */
  if (sleeping_time > 0)
    usleep (sleeping_time);

  driver->last_wait_ust = jack_get_microseconds ();
  driver->engine->transport_cycle_start (driver->engine, driver->last_wait_ust);

  *status = 0;
  *delayed_usecs = driver->last_wait_ust - starting_time - sleeping_time;

  return driver->period_size;
}

static int
dummy_driver_null_cycle (dummy_driver_t* driver, jack_nframes_t nframes)
{
  return 0;
}

static int
dummy_driver_read (dummy_driver_t *driver, jack_nframes_t nframes)
{
  return 0;
}

static int
dummy_driver_write (dummy_driver_t* driver, jack_nframes_t nframes)
{
  return 0;
}


static int
dummy_driver_attach (dummy_driver_t *driver, jack_engine_t *engine)
{
  jack_port_t * port;
  char buf[32];
  unsigned int chn;
  int port_flags;

  driver->engine = engine;

  driver->engine->set_buffer_size (engine, driver->period_size);
  driver->engine->set_sample_rate (engine, driver->sample_rate);

  port_flags = JackPortIsOutput|JackPortIsPhysical|JackPortIsTerminal;

  for (chn = 0; chn < driver->capture_channels; chn++)
    {
      snprintf (buf, sizeof(buf) - 1, "capture_%u", chn+1);

      port = jack_port_register (driver->client, buf, JACK_DEFAULT_AUDIO_TYPE, port_flags, 0);
      if (!port)
	{
	  jack_error ("DUMMY: cannot register port for %s", buf);
	  break;
	}

      driver->capture_ports = jack_slist_append (driver->capture_ports, port);
    }
	
  port_flags = JackPortIsInput|JackPortIsPhysical|JackPortIsTerminal;

  for (chn = 0; chn < driver->playback_channels; chn++)
    {
      snprintf (buf, sizeof(buf) - 1, "playback_%u", chn+1);

      port = jack_port_register (driver->client, buf, JACK_DEFAULT_AUDIO_TYPE, port_flags, 0);

      if (!port)
	{
	  jack_error ("DUMMY: cannot register port for %s", buf);
	  break;
	}

      driver->playback_ports = jack_slist_append (driver->playback_ports, port);
    }

  jack_activate (driver->client);

  return 0;
}

static void
dummy_driver_detach (dummy_driver_t *driver, jack_engine_t *engine)
{
  JSList * node;

  if (driver->engine == 0)
    return;

  for (node = driver->capture_ports; node; node = jack_slist_next (node))
    jack_port_unregister (driver->client, ((jack_port_t *) node->data));

  jack_slist_free (driver->capture_ports);
  driver->capture_ports = NULL;

		
  for (node = driver->playback_ports; node; node = jack_slist_next (node))
    jack_port_unregister (driver->client, ((jack_port_t *) node->data));

  jack_slist_free (driver->playback_ports);
  driver->playback_ports = NULL;
	
  driver->engine = NULL;
}

static void
dummy_driver_delete (dummy_driver_t *driver)
{
  free (driver);
}

static jack_driver_t *
dummy_driver_new (jack_client_t * client,
		  char *name,
		  unsigned int capture_ports,
		  unsigned int playback_ports,
		  jack_nframes_t sample_rate,
		  jack_nframes_t period_size,
		  unsigned long wait_time)
{
  dummy_driver_t * driver;

  printf ("creating dummy driver ... %s|%" PRIu32 "|%" PRIu32
	  "|%lu|%u|%u\n", name, sample_rate, period_size, wait_time,
	  capture_ports, playback_ports);

  driver = (dummy_driver_t *) calloc (1, sizeof (dummy_driver_t));

  jack_driver_init ((jack_driver_t *) driver);

  driver->attach     = (JackDriverAttachFunction)    dummy_driver_attach;
  driver->detach     = (JackDriverDetachFunction)    dummy_driver_detach;
  driver->wait       = (JackDriverWaitFunction)      dummy_driver_wait;
  driver->read       = (JackDriverReadFunction)      dummy_driver_read;
  driver->write      = (JackDriverReadFunction)      dummy_driver_write;
  driver->null_cycle = (JackDriverNullCycleFunction) dummy_driver_null_cycle;
  driver->start      = (JackDriverStartFunction)     dummy_driver_audio_start;
  driver->stop       = (JackDriverStopFunction)      dummy_driver_audio_stop;

  driver->period_usecs =
    (jack_time_t) floor ((((float) period_size) / sample_rate) * 1000000.0f);
  driver->sample_rate = sample_rate;
  driver->period_size = period_size;
  driver->wait_time   = wait_time;
  driver->last_wait_ust = 0;

  driver->capture_channels  = capture_ports;
  driver->capture_ports     = NULL;
  driver->playback_channels = playback_ports;
  driver->playback_ports    = NULL;

  driver->client = client;
  driver->engine = NULL;

  return (jack_driver_t *) driver;
}

static void
dummy_usage ()
{
	fprintf (stderr, "\n"
"dummy driver arguments:\n"
"    -h,--help    \tprint this message\n"
"    -r,--rate <n>      \tsample rate (default: 48000)\n"
"    -p,--period <n>    \tframes per period (default: 1024)\n"
"    -C,--capture <n> \tnumber of capture ports (default: 2)\n"
"    -P,--playback <n> \tnumber of playback ports (default: 2)\n"
"    -w,--wait <usecs> \tnumber of usecs to wait between engine processes (default: 21333)\n"
"\n");
}

static void
dummy_error (char *type, char *value)
{
	fprintf (stderr, "dummy driver: unknown %s: `%s'\n", type, value);
	dummy_usage();
}



/* DRIVER "PLUGIN" INTERFACE */

const char driver_client_name[] = "dummy_pcm";

jack_driver_t *
driver_initialize (jack_client_t *client, int argc, char **argv)
{
	jack_nframes_t sample_rate = 48000;
	jack_nframes_t period_size = 1024;
	unsigned int capture_ports = 2;
	unsigned int playback_ports = 2;
	int wait_time_set = 0;
	unsigned long wait_time;

	int opt;
	char optstring[2];		/* string made from opt char */
	struct option long_options[] = 
	{ 
		{ "help",	no_argument, NULL, 'h' },
		{ "rate",	required_argument, NULL, 'r' },
		{ "period",	required_argument, NULL, 'p' },
		{ "capture",	required_argument, NULL, 'C' },
		{ "playback",	required_argument, NULL, 'P' },
		{ "wait",	required_argument, NULL, 'w' },
		{ 0, 0, 0, 0 }
	};

		
	/*
	 * Setting optind back to zero is a hack to reinitialize a new
	 * getopts() loop.  See declaration in <getopt.h>.
	 */

	optind = 0;
	opterr = 0;

	while ((opt = getopt_long(argc, argv, "C::P::p:r:w:h",
				  long_options, NULL))
	       != EOF) {
		switch (opt) {

		case 'C':
		  capture_ports = atoi (optarg);
		  break;

		case 'P':
		  playback_ports = atoi (optarg);
		  break;

		case 'p':
		  period_size = atoi(optarg);
		  break;
				
		case 'r':
		  sample_rate = atoi(optarg);
		  break;
				
		case 'w':
		  wait_time = strtoul(optarg, NULL, 10);
		  wait_time_set = 1;
		  break;
				
		case 'h':
		  dummy_usage();
		  return NULL;

		/* the rest is error handling: */
		case 1:			/* not an option */
		  dummy_error("parameter", optarg);
		  return NULL;
				
		default:		/* unrecognized option */
		  optstring[0] = (char) optopt;
		  optstring[1] = '\0';
		  dummy_error("option", optstring);
		  return NULL;
		}
	}

        if (!wait_time_set)
	  wait_time = (((float)period_size) / ((float)sample_rate)) * 1000000.0;

	return dummy_driver_new (client, "dummy_pcm", capture_ports,
				 playback_ports, sample_rate, period_size,
				 wait_time);
}

void
driver_finish (jack_driver_t *driver)
{
	dummy_driver_delete ((dummy_driver_t *) driver);
}
