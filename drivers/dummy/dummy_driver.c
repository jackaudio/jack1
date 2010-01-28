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

*/

#include <math.h>
#include <stdio.h>
#include <memory.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/mman.h>

#include <jack/types.h>
#include <jack/internal.h>
#include <jack/engine.h>
#include <sysdeps/time.h>

#include "dummy_driver.h"

#undef DEBUG_WAKEUP

/* this is used for calculate what counts as an xrun */
#define PRETEND_BUFFER_SIZE 4096

void
FakeVideoSync( dummy_driver_t *driver )
{
        #define VIDEO_SYNC_PERIOD (48000 / 30)
        static int vidCounter = VIDEO_SYNC_PERIOD;
        
        int period = driver->period_size;
        jack_position_t *position = &driver->engine->control->current_time;

        if ( period >= VIDEO_SYNC_PERIOD ) {
                jack_error("JACK driver period size too large for simple video sync emulation. Halting.");
                exit(0);
        }

        //enable video sync, whether it occurs in this period or not
        position->audio_frames_per_video_frame = VIDEO_SYNC_PERIOD;
        position->valid = (jack_position_bits_t) (position->valid | JackAudioVideoRatio);

        //no video pulse found in this period, just decrement the counter
        if ( vidCounter > period ) {
                vidCounter -= period;
        }

        //video pulse occurs in this period
        if ( vidCounter <= period ) {
                int remainder = period - vidCounter;
                vidCounter = VIDEO_SYNC_PERIOD - remainder;

                position->video_offset = vidCounter;
                position->valid = (jack_position_bits_t) (position->valid | JackVideoFrameOffset);
        }
}

#ifdef HAVE_CLOCK_GETTIME
static inline unsigned long long ts_to_nsec(struct timespec ts)
{
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static inline struct timespec nsec_to_ts(unsigned long long nsecs)
{
    struct timespec ts;
    ts.tv_sec = nsecs / (1000000000LL);
    ts.tv_nsec = nsecs % (1000000000LL);
    return ts;
}

static inline struct timespec add_ts(struct timespec ts, unsigned int usecs)
{
    unsigned long long nsecs = ts_to_nsec(ts);
    nsecs += usecs * 1000LL;
    return nsec_to_ts(nsecs);
}

static inline int cmp_lt_ts(struct timespec ts1, struct timespec ts2)
{
    if(ts1.tv_sec < ts2.tv_sec) {
        return 1;
    } else if (ts1.tv_sec == ts2.tv_sec && ts1.tv_nsec < ts2.tv_nsec) {
        return 1;
    } else return 0;
}

static jack_nframes_t 
dummy_driver_wait (dummy_driver_t *driver, int extra_fd, int *status,
		   float *delayed_usecs)
{
	jack_nframes_t nframes = driver->period_size;
	struct timespec now;

	*status = 0;
	/* this driver doesn't work so well if we report a delay */
	*delayed_usecs = 0;		/* lie about it */

	clock_gettime(CLOCK_REALTIME, &now);
	
	if (cmp_lt_ts(driver->next_wakeup, now)) {
		if (driver->next_wakeup.tv_sec == 0) {
			/* first time through */
			clock_gettime(CLOCK_REALTIME, &driver->next_wakeup);
		}  else if ((ts_to_nsec(now) - ts_to_nsec(driver->next_wakeup))/1000LL
			    > (PRETEND_BUFFER_SIZE * 1000000LL
			       / driver->sample_rate)) {
			/* xrun */
			jack_error("**** dummy: xrun of %ju usec",
				(uintmax_t)(ts_to_nsec(now) - ts_to_nsec(driver->next_wakeup))/1000LL);
			nframes = 0;
		} else {
			/* late, but handled by our "buffer"; try to
			 * get back on track */
		}
		driver->next_wakeup = add_ts(driver->next_wakeup, driver->wait_time);
	} else {
		if(clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &driver->next_wakeup, NULL)) {
			jack_error("error while sleeping");
			*status = -1;
		} else {
			clock_gettime(CLOCK_REALTIME, &now);
			// guaranteed to sleep long enough for this to be correct
			*delayed_usecs = (ts_to_nsec(now) - ts_to_nsec(driver->next_wakeup));
			*delayed_usecs /= 1000.0;
		}
		driver->next_wakeup = add_ts(driver->next_wakeup, driver->wait_time);
	}

	driver->last_wait_ust = jack_get_microseconds ();
	driver->engine->transport_cycle_start (driver->engine,
					       driver->last_wait_ust);

	return nframes;
}

#else

static jack_nframes_t 
dummy_driver_wait (dummy_driver_t *driver, int extra_fd, int *status,
		   float *delayed_usecs)
{
	jack_time_t now = jack_get_microseconds();

	if (driver->next_time < now) {
		if (driver->next_time == 0) {
			/* first time through */
			driver->next_time = now + driver->wait_time;
		}  else if (now - driver->next_time
			    > (PRETEND_BUFFER_SIZE * 1000000LL
			       / driver->sample_rate)) {
			/* xrun */
			jack_error("**** dummy: xrun of %ju usec",
				(uintmax_t)now - driver->next_time);
			driver->next_time = now + driver->wait_time;
		} else {
			/* late, but handled by our "buffer"; try to
			 * get back on track */
			driver->next_time += driver->wait_time;
		}
	} else {
		jack_time_t wait = driver->next_time - now;
		struct timespec ts = { .tv_sec = wait / 1000000,
				       .tv_nsec = (wait % 1000000) * 1000 };
		nanosleep(&ts,NULL);
		driver->next_time += driver->wait_time;
	}

	driver->last_wait_ust = jack_get_microseconds ();
	driver->engine->transport_cycle_start (driver->engine,
					       driver->last_wait_ust);

	/* this driver doesn't work so well if we report a delay */
	*delayed_usecs = 0;		/* lie about it */
	*status = 0;
	return driver->period_size;
}
#endif

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
		engine->delay (engine, delayed_usecs);
		return 0;
	} 

	// FakeVideoSync (driver);

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
	driver->period_size = nframes;  
	driver->period_usecs = driver->wait_time =
		(jack_time_t) floor ((((float) nframes) / driver->sample_rate)
				     * 1000000.0f);

	/* tell the engine to change its buffer size */
	if (driver->engine->set_buffer_size (driver->engine, nframes)) {
		jack_error ("dummy: cannot set engine buffer size to %d (check MIDI)", nframes);
		return -1;
	}

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

	if (driver->engine->set_buffer_size (driver->engine, driver->period_size)) {
		jack_error ("dummy: cannot set engine buffer size to %d (check MIDI)", driver->period_size);
		return -1;
	}
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

	jack_info ("creating dummy driver ... %s|%" PRIu32 "|%" PRIu32
		"|%lu|%u|%u", name, sample_rate, period_size, wait_time,
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
	//driver->next_time   = 0; // not needed since calloc clears the memory
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
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 2U;
	strcpy (params[i].short_desc, "Number of capture ports");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "playback");
	params[i].character  = 'P';
	params[i].type       = JackDriverParamUInt;
	params[1].value.ui   = 2U;
	strcpy (params[i].short_desc, "Number of playback ports");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "rate");
	params[i].character  = 'r';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 48000U;
	strcpy (params[i].short_desc, "Sample rate");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "period");
	params[i].character  = 'p';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 1024U;
	strcpy (params[i].short_desc, "Frames per period");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "wait");
	params[i].character  = 'w';
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
	unsigned long wait_time = 0;
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

