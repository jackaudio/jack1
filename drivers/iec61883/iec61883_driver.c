/* -*- mode: c; c-file-style: "linux"; -*- */
/*
 *   JACK IEC16883 (FireWire audio) driver
 *
 *   Copyright (C) 2003 Robert Ham <rah@bash.sh>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <jack/engine.h>
#include <jack/jslist.h>
#include <jack/port.h>

#include "iec61883_driver.h"
#include "iec61883_common.h"

static int iec61883_driver_stop (iec61883_driver_t *driver);

static int
iec61883_driver_attach (iec61883_driver_t *driver)
{
	int err;

	driver->engine->set_buffer_size (driver->engine, driver->buffer_size);
	driver->engine->set_sample_rate (driver->engine, driver->iec61883_client->sample_rate);

	err = iec61883_client_create_ports (driver->iec61883_client);
	if (err) {
		return err;
	} 
 
	jack_activate (driver->jack_client);

	return 0;
}

static int
iec61883_driver_detach (iec61883_driver_t *driver)
{
	iec61883_client_destroy_ports (driver->iec61883_client);
	return 0;
}

static int
iec61883_driver_read (iec61883_driver_t * driver, jack_nframes_t nframes)
{
	return iec61883_client_read (driver->iec61883_client, nframes);
}

static int
iec61883_driver_write (iec61883_driver_t * driver, jack_nframes_t nframes)
{
	return iec61883_client_write (driver->iec61883_client, nframes);
}

static int
iec61883_driver_run_cycle (iec61883_driver_t *driver)
{
	float delayed_usecs = 0.0f;
	int err;

/*	VERBOSE (driver->engine, "IEC61883: running client cycle"); */

	err = iec61883_client_run_cycle (driver->iec61883_client);
	if (err) {
 	        jack_error ("IEC61883: client cycle error");
		return err;
	}

	return driver->engine->run_cycle (driver->engine, driver->buffer_size, delayed_usecs);
}


static int
iec61883_driver_start (iec61883_driver_t *driver)
{
	int err;

	err = iec61883_client_start (driver->iec61883_client);
	if (err) {
		return err;
	}

	return 0;
}

static int
iec61883_driver_stop (iec61883_driver_t *driver)
{
	iec61883_client_stop (driver->iec61883_client);
	return 0;
}

typedef void (*JackDriverFinishFunction) (jack_driver_t *);

static iec61883_driver_t *
iec61883_driver_new (jack_client_t *jack_client,
                     int port,
                     enum raw1394_iso_speed speed,
                     int irq_interval,
                     jack_nframes_t period_size, 
                     jack_nframes_t buffer_size,
                     jack_nframes_t sample_rate,
                     JSList * capture_channels,
                     JSList * playback_channels)
{
	iec61883_driver_t *driver;
	iec61883_client_t *client;

	{
		const char * speed_str;

		switch (speed) {
		case RAW1394_ISO_SPEED_100:
			speed_str = "100";
			break;
		case RAW1394_ISO_SPEED_200:
			speed_str = "200";
			break;
		case RAW1394_ISO_SPEED_400:
			speed_str = "400";
			break;
		}

		printf ("Creating IEC61883 driver: %d|%s|%d|%"
			PRIu32 "|%" PRIu32 "|%" PRIu32 "|",
			port, speed_str, irq_interval,
			period_size, buffer_size, sample_rate);

		if (capture_channels) {
			iec61883_client_print_iso_ch_info (capture_channels, stdout);
			putchar ('|');
		} else {
			printf ("-|");
		}

		if (playback_channels) {
			iec61883_client_print_iso_ch_info (playback_channels, stdout);
		} else {
			printf ("-");
		}

		putchar ('\n');
	}


	/* start up the iec61883 client */
	client = iec61883_client_new (jack_client, buffer_size, buffer_size, sample_rate,
                                      port, speed, irq_interval,
				      capture_channels, playback_channels);
	if (!client)
		return NULL;

    
	driver = calloc (1, sizeof (iec61883_driver_t));
  
	jack_driver_nt_init ((jack_driver_nt_t *) driver);

	driver->nt_attach    = (JackDriverNTAttachFunction)   iec61883_driver_attach;
	driver->nt_detach    = (JackDriverNTDetachFunction)   iec61883_driver_detach;
	driver->nt_start     = (JackDriverNTStartFunction)    iec61883_driver_start;
	driver->nt_stop      = (JackDriverNTStopFunction)     iec61883_driver_stop;
	driver->nt_run_cycle = (JackDriverNTRunCycleFunction) iec61883_driver_run_cycle;
 	if (capture_channels) {
		driver->read  = (JackDriverReadFunction) iec61883_driver_read;
	}
	if (playback_channels) {
		driver->write = (JackDriverReadFunction) iec61883_driver_write;
	}
  
	driver->jack_client     = jack_client;
	driver->iec61883_client = client;
	driver->buffer_size     = buffer_size;
  
	return driver;
}

static void
iec61883_driver_delete (iec61883_driver_t *driver)
{
	iec61883_client_destroy (driver->iec61883_client);
	free (driver);
}


/*
 * dlopen plugin stuff
 */

const char driver_client_name[] = "firewire_pcm";

/* 
   "\n"
   "  Eg, 1,3-5,6:1,7-9:8 specifies that isochronous channels 1 and 3 to 5 are to be\n"
   "  used to transmit/recieve a stereo pair of audio channels each.  Channel 6 is to\n"
   "  be used to transmit/recieve one audio channel.  Channels 7 to 9 are to be used\n"
   "  to transmit/recieve 8 audio channels each.\n"
   "\n"
*/


const jack_driver_desc_t *
driver_get_descriptor ()
{
	jack_driver_desc_t * desc;
	jack_driver_param_desc_t * params;
	unsigned int i;

	desc = calloc (1, sizeof (jack_driver_desc_t));

	strcpy (desc->name, "iec61883");
	desc->nparams = 7;
  
	params = calloc (desc->nparams, sizeof (jack_driver_param_desc_t));
	desc->params = params;

	i = 0;
	strcpy (params[i].name, "capture");
	params[i].character  = 'C';
	params[i].type       = JackDriverParamString;
	strcpy (params[i].value.str, "");
	strcpy (params[i].short_desc,
		"Which channels to capture on (eg, 1-3:2,5-8:1)");
	strcpy (params[i].long_desc,
		"A channel spec is a comma-seperated list of ranges of the form i[-j][:k] where "
		"i and j are isochronous channel numbers and k is an audio channel count.  By "
		"itself, i describes a single isochronous channel.  If j is present, i and j "
		"describe a range of channels, i being the first and j the last.  If k is present, "
		"it indicates the number of audio channels to send over each isochronous channel."
		"If ommited, isochronous channels default to 2 audio channels.");

	i++;
	strcpy (params[i].name, "playback");
	params[i].character  = 'P';
	params[i].type       = JackDriverParamString;
	strcpy (params[i].value.str, "");
	strcpy (params[i].short_desc,
		"Which channels to playback on (eg, 1-3:2,5-8:1)");
	strcpy (params[i].long_desc, params[i-1].long_desc);

	i++;
	strcpy (params[i].name, "port");
	params[i].character  = 'd';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 0U;
	strcpy (params[i].short_desc, "The firewire port (ie, device) to use");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "buffer-size");
	params[i].character  = 'b';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 1024U;
	strcpy (params[i].short_desc, "The buffer size to use (in frames)");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "irq-interval");
	params[i].character  = 'i';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 0U;
	strcpy (params[i].short_desc, "The interrupt interval to use (in packets)");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "sample-rate");
	params[i].character  = 'r';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 48000U;
	strcpy (params[i].short_desc, "Sample rate to use");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "speed");
	params[i].character  = 's';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 400U;
	strcpy (params[i].short_desc, "Set the transmit speed to 400, 200 or 100 MB/s");
	strcpy (params[i].long_desc, params[i].short_desc);


	return desc;
}


jack_driver_t *
driver_initialize (jack_client_t *client, JSList * params)
{
	JSList * capture_channels = NULL;
	JSList * playback_channels = NULL;
	int port = -1;
	int period_size_set = 0;
	jack_nframes_t period_size = 0;
	int buffer_size_set = 0;
	jack_nframes_t buffer_size;
	int sample_rate_set = 0;
	jack_nframes_t sample_rate = 0;
	iec61883_driver_t * driver;
	int speed = -1;
	int irq_interval = -1;
	jack_driver_param_t * param;
	JSList * node;

	for (node = params; node; node = jack_slist_next (node))
	{
		param = (jack_driver_param_t *) node->data;

		switch (param->character)
		{
		case 'C':
			capture_channels = iec61883_get_channel_spec (param->value.str);
			break;
        
		case 'P':
			playback_channels = iec61883_get_channel_spec (param->value.str);
			break;
          
		case 'd':
			port = param->value.ui;
			break;
        
/*        case 'p':
          period_size = param->value.ui;
          period_size_set = 1;
          break; */
        
        
		case 'b':
			buffer_size = param->value.ui;
			buffer_size_set = 1;
			break;
        
		case 'i':
			irq_interval = param->value.ui;
			break;
        
		case 'r':
			sample_rate = param->value.ui;
			sample_rate_set = 1;
			break;
        
		case 's':
			speed = param->value.ui;
			break;
		}
	}
  
	if (!capture_channels && !playback_channels)
	{
		jack_error ("IEC61883: no capture or playback channels specified");
		return NULL;
	}
  
	if (port == -1)
		port = 0;
  
	if (!period_size_set)
		period_size = 1024;

/*  if (!periods_set)
    periods = 2; */
  
	if (!buffer_size_set)
		buffer_size = 1024;

	if (!sample_rate_set)
		sample_rate = 48000;
  
	if (speed == -1)
		speed = RAW1394_ISO_SPEED_400;
	else
	{
		switch (speed)
		{
		case 400:
			speed = RAW1394_ISO_SPEED_400;
			break;
		case 200:
			speed = RAW1394_ISO_SPEED_200;
			break;
		case 100:
			speed = RAW1394_ISO_SPEED_100;
			break;
		default:
			jack_error ("IEC61883: invalid speed %d MB/s; must be 400, 200 or 100 MB/s", speed);
			return NULL;
		}
	}
  
	driver = 
		iec61883_driver_new (client, port, speed, irq_interval,
				     period_size, buffer_size, sample_rate,
				     capture_channels, playback_channels);
  
	return (jack_driver_t *) driver;
}

void
driver_finish (jack_driver_t *driver)
{
	iec61883_driver_delete ((iec61883_driver_t *) driver);
}
