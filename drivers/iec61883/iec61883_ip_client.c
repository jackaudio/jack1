/* -*- mode: c; c-file-style: "linux"; -*- */
/*
 *   JACK IEC61883 (FireWire audio) driver
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

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <poll.h>

#include <jack/internal.h>

#include "iec61883_ip_client.h"
#include "iec61883_common.h"

static iec61883_client_t * iec61883_client;

static void *
iec61883_ip_client_run (void *arg)
{
	iec61883_client_t * client = arg;
	int err;

/*
	err = jack_acquire_real_time_scheduling (pthread_self (),
						 client->jack_client->engine->engine_priority + 1);
	if (err) {
		jack_error ("IEC61883IP: failed to acquire real time scheduling for "
			    "client thread; things probably won't work");
	}
*/

	err = iec61883_client_main (client, pthread_self ());
	if (err) {
		jack_error ("IEC61883IP: client thread errored out");
	}

	jack_error ("IEC61883IP: client thread finished");

	return NULL;
}

static int
iec61883_ip_client_start (iec61883_client_t * client)
{
	int err;
	pthread_t thread;

	err = pthread_create (&thread, NULL,
			      iec61883_ip_client_run, (void *) client);

	if (err) {
		jack_error ("IEC61883IP: could not start iec61883 client thread: "
			    "%s", strerror (err));
		return -1;
	}

	return 0;
}

static int
iec61883_ip_client_process (jack_nframes_t nframes, void * arg)
{
	iec61883_client_t * client = arg;
	int err;

	err = iec61883_client_run_cycle (client);
	if (err) {
		jack_error ("IEC61883IP: client cycle failed");
		return err;
	}
		jack_error ("IEC61883IP: client cycle complete");

	if (client->cap_chs) {
		err = iec61883_client_read (client, nframes);
		if (err) {
			jack_error ("IEC61883IP: client read failed");
			return err;
		}
	}

	if (client->play_chs) {
		err = iec61883_client_write (client, nframes);
		if (err) {
			jack_error ("IEC61883IP: client write failed");
			return err;
		}
	}

	return 0;
}

int
jack_initialize (jack_client_t * jack_client, const char * data)
{
	jack_nframes_t fifo_size;
	int port = 0;
	enum raw1394_iso_speed speed = RAW1394_ISO_SPEED_400;
	int irq_interval = -1;
	JSList * cap_chs = NULL;
	JSList * play_chs = NULL;
	int err;

	char * params;
	char * param;
	char * arg;

	fifo_size = jack_get_buffer_size (jack_client);

	params = strdup (data);

	param = strtok (params, ",");
	do {
		arg = strchr (param, '=');
		if (!arg) {
			jack_error ("IEC61883IP: data must be of the form "
				    "<param1>=<arg1>[,<param2>=<arg2>[, ... ]]");
			free (params);
			return -1;
		}
		*arg = '\0';
		arg++;

		if (strcmp (param, "fifo_size") == 0) {
			fifo_size = atoi (arg);

		} else if (strcmp (param, "port") == 0) {
			port = atoi (arg);

		} else if (strcmp (param, "speed") == 0) {
			switch (atoi (arg)) {
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
				jack_error ("IEC61883IP: invalid speed %d MB/s; "
					    "must be 400, 200 or 100 MB/s", speed);
				free (params);
				return -1;
			}

		} else if (strcmp (param, "irq_interval") == 0) {
			irq_interval = atoi (arg);

		} else if (strcmp (param, "capture") == 0) {
			cap_chs = iec61883_get_channel_spec (arg);

		} else if (strcmp (param, "playback") == 0) {
			play_chs = iec61883_get_channel_spec (arg);
		}

	} while ( (param = strtok (NULL, ",")) );
	free (params);



	iec61883_client = iec61883_client_new (
		jack_client,
		jack_get_buffer_size (jack_client),
		fifo_size,
		jack_get_sample_rate (jack_client),
		port,
		speed,
		irq_interval,
		cap_chs,
		play_chs);

	if (!iec61883_client) {
		return -1;
	}

	jack_set_process_callback (jack_client,
				   iec61883_ip_client_process,
				   iec61883_client);

	err = iec61883_client_create_ports (iec61883_client);
	if (err) {
		iec61883_client_destroy (iec61883_client);
		return -1;
	}

/*	err = iec61883_ip_client_start (iec61883_client);
	if (err) {
		jack_deactivate (jack_client);
		iec61883_client_destroy (iec61883_client);
		return -1;
		}*/

	jack_activate (jack_client);

	return 0;
}

void
jack_finish (void)
{
	iec61883_client_main_stop     (iec61883_client);
	iec61883_client_stop          (iec61883_client);
	iec61883_client_destroy_ports (iec61883_client);
	iec61883_client_destroy       (iec61883_client);
}

/* EOF */

