/* -*- mode: c; c-file-style: "linux"; -*- */
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
#include <sys/mman.h>

#include <jack/types.h>
#include <jack/internal.h>
#include <jack/engine.h>
#include <jack/time.h>

#include "dummy_driver.h"

#undef DEBUG_WAKEUP


static jack_nframes_t 
dummy_driver_wait (dummy_driver_t *driver, int extra_fd, int *status,
		   float *delayed_usecs)
{
	jack_time_t starting_time = jack_get_microseconds();
	jack_time_t processing_time = (driver->last_wait_ust?
				       starting_time - driver->last_wait_ust:
				       0);

	/* wait until time for next cycle */
	if (driver->wait_time > processing_time)
		usleep (driver->wait_time - processing_time);

	driver->last_wait_ust = jack_get_microseconds ();
	driver->engine->transport_cycle_start (driver->engine,
					       driver->last_wait_ust);

	/* this driver doesn't work so well if we report a delay */
	*delayed_usecs = 0;		/* lie about it */
	*status = 0;
	return driver->period_size;
}

static inline int
dummy_driver_run_cycle (dummy_driver_t *driver)
{
	jack_engine_t *engine = driver->engine;
	int wait_status;
	float delayed_usecs;

	jack_nframes_t nframes = dummy_driver_wait (driver, -1, &wait_status,
						   &delayed_usecs);
	if (nframes == 0) {
		/* we detected an xrun and restarted: notify
		 * clients about the delay. */
		engine->delay (engine);
		return 0;
	} 

	if (wait_status == 0)
		return engine->run_cycle (engine, nframes, delayed_usecs);

	if (wait_status < 0)
		return -1;
	else
		return 0;
}

static int
dummy_driver_null_cycle (dummy_driver_t* driver, jack_nframes_t nframes)
{
	return 0;
}

static int
dummy_driver_bufsize (dummy_driver_t* driver, jack_nframes_t nframes)
{
	/* This is a somewhat arbitrary size restriction.  The dummy
	 * driver doesn't work well with smaller buffer sizes,
	 * apparantly due to usleep() inaccuracy under Linux 2.4.  If
	 * you can get it working with smaller buffers, lower the
	 * limit.  (JOQ) */
	if (nframes < 128)
		return EINVAL;

	driver->period_size = nframes;  
	driver->period_usecs = driver->wait_time =
		(jack_time_t) floor ((((float) nframes) / driver->sample_rate)
				     * 1000000.0f);

	/* tell the engine to change its buffer size */
	driver->engine->set_buffer_size (driver->engine, nframes);

	return 0;
}

static int
dummy_driver_write (dummy_driver_t* driver, jack_nframes_t nframes)
{
	return 0;
}


static int
dummy_driver_attach (dummy_driver_t *driver)
{
	jack_port_t * port;
	char buf[32];
	unsigned int chn;
	int port_flags;

	driver->engine->set_buffer_size (driver->engine, driver->period_size);
	driver->engine->set_sample_rate (driver->engine, driver->sample_rate);

	port_flags = JackPortIsOutput|JackPortIsPhysical|JackPortIsTerminal;

	for (chn = 0; chn < driver->capture_channels; chn++)
	{
		snprintf (buf, sizeof(buf) - 1, "capture_%u", chn+1);

		port = jack_port_register (driver->client, buf,
					   JACK_DEFAULT_AUDIO_TYPE,
					   port_flags, 0);
		if (!port)
		{
			jack_error ("DUMMY: cannot register port for %s", buf);
			break;
		}

		driver->capture_ports =
			jack_slist_append (driver->capture_ports, port);
	}
	
	port_flags = JackPortIsInput|JackPortIsPhysical|JackPortIsTerminal;

	for (chn = 0; chn < driver->playback_channels; chn++)
	{
		snprintf (buf, sizeof(buf) - 1, "playback_%u", chn+1);

		port = jack_port_register (driver->client, buf,
					   JACK_DEFAULT_AUDIO_TYPE,
					   port_flags, 0);

		if (!port)
		{
			jack_error ("DUMMY: cannot register port for %s", buf);
			break;
		}

		driver->playback_ports =
			jack_slist_append (driver->playback_ports, port);
	}

	jack_activate (driver->client);

	return 0;
}

static int
dummy_driver_detach (dummy_driver_t *driver)
{
	JSList * node;

	if (driver->engine == 0)
		return 0;

	for (node = driver->capture_ports; node; node = jack_slist_next (node))
		jack_port_unregister (driver->client,
				      ((jack_port_t *) node->data));

	jack_slist_free (driver->capture_ports);
	driver->capture_ports = NULL;

		
	for (node = driver->playback_ports; node; node = jack_slist_next (node))
		jack_port_unregister (driver->client,
				      ((jack_port_t *) node->data));

	jack_slist_free (driver->playback_ports);
	driver->playback_ports = NULL;

	return 0;
}


static void
dummy_driver_delete (dummy_driver_t *driver)
{
	jack_driver_nt_finish ((jack_driver_nt_t *) driver);
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

	jack_driver_nt_init ((jack_driver_nt_t *) driver);

	driver->write         = (JackDriverReadFunction)       dummy_driver_write;
	driver->null_cycle    = (JackDriverNullCycleFunction)  dummy_driver_null_cycle;
	driver->nt_attach     = (JackDriverNTAttachFunction)   dummy_driver_attach;
	driver->nt_detach     = (JackDriverNTDetachFunction)   dummy_driver_detach;
	driver->nt_bufsize    = (JackDriverNTBufSizeFunction)  dummy_driver_bufsize;
	driver->nt_run_cycle  = (JackDriverNTRunCycleFunction) dummy_driver_run_cycle;

	driver->period_usecs =
		(jack_time_t) floor ((((float) period_size) / sample_rate)
				     * 1000000.0f);
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


/* DRIVER "PLUGIN" INTERFACE */

jack_driver_desc_t *
driver_get_descriptor ()
{
	jack_driver_desc_t * desc;
	jack_driver_param_desc_t * params;
	unsigned int i;

	desc = calloc (1, sizeof (jack_driver_desc_t));
	strcpy (desc->name, "dummy");
	desc->nparams = 5;

	params = calloc (desc->nparams, sizeof (jack_driver_param_desc_t));

	i = 0;
	strcpy (params[i].name, "capture");
	params[i].character  = 'C';
	params[i].has_arg    = required_argument;
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 2U;
	strcpy (params[i].short_desc, "Number of capture ports");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "playback");
	params[i].character  = 'P';
	params[i].has_arg    = required_argument;
	params[i].type       = JackDriverParamUInt;
	params[1].value.ui   = 2U;
	strcpy (params[i].short_desc, "Number of playback ports");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "rate");
	params[i].character  = 'r';
	params[i].has_arg    = required_argument;
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 48000U;
	strcpy (params[i].short_desc, "Sample rate");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "period");
	params[i].character  = 'p';
	params[i].has_arg    = required_argument;
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 1024U;
	strcpy (params[i].short_desc, "Frames per period");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "wait");
	params[i].character  = 'w';
	params[i].has_arg    = required_argument;
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 21333U;
	strcpy (params[i].short_desc,
		"Number of usecs to wait between engine processes");
	strcpy (params[i].long_desc, params[i].short_desc);

	desc->params = params;

	return desc;
}

const char driver_client_name[] = "dummy_pcm";

jack_driver_t *
driver_initialize (jack_client_t *client, const JSList * params)
{
	jack_nframes_t sample_rate = 48000;
	jack_nframes_t period_size = 1024;
	unsigned int capture_ports = 2;
	unsigned int playback_ports = 2;
	int wait_time_set = 0;
	unsigned long wait_time;
	const JSList * node;
	const jack_driver_param_t * param;

	for (node = params; node; node = jack_slist_next (node)) {
  	        param = (const jack_driver_param_t *) node->data;

		switch (param->character) {

		case 'C':
		  capture_ports = param->value.ui;
		  break;

		case 'P':
		  playback_ports = param->value.ui;
		  break;

		case 'r':
		  sample_rate = param->value.ui;
		  break;

		case 'p':
		  period_size = param->value.ui;
		  break;

		case 'w':
		  wait_time = param->value.ui;
		  wait_time_set = 1;
		  break;
				
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
