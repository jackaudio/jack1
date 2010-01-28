/*
 *   FireWire Backend for Jack
 *   using FFADO
 *   FFADO = Firewire (pro-)audio for linux
 *
 *   http://www.ffado.org
 *   http://www.jackaudio.org
 *
 *   Copyright (C) 2005-2007 Pieter Palmers
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
 */

/* 
 * Main Jack driver entry routines
 *
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
#include <assert.h>

#include "ffado_driver.h"

#define SAMPLE_MAX_24BIT  8388608.0f
#define SAMPLE_MAX_16BIT  32768.0f

static int ffado_driver_stop (ffado_driver_t *driver);

#define FIREWIRE_REQUIRED_FFADO_API_VERSION 8

// enable verbose messages
static int g_verbose=0;

static int
ffado_driver_attach (ffado_driver_t *driver)
{
	char buf[64];
	char buf2[64];
	channel_t chn;
	jack_port_t *port=NULL;
	int port_flags;

	g_verbose=driver->engine->verbose;

	if (driver->engine->set_buffer_size (driver->engine, driver->period_size)) {
		jack_error ("FFADO: cannot set engine buffer size to %d (check MIDI)", driver->period_size);
		return -1;
	}
	driver->engine->set_sample_rate (driver->engine, driver->sample_rate);

	/* preallocate some buffers such that they don't have to be allocated
	   in RT context (or from the stack)
	 */
	/* the null buffer is a buffer that contains one period of silence */
	driver->nullbuffer = calloc(driver->period_size, sizeof(ffado_sample_t));
	if(driver->nullbuffer == NULL) {
		printError("could not allocate memory for null buffer");
		return -1;
	}
	/* calloc should do this, but it can't hurt to be sure */
	memset(driver->nullbuffer, 0, driver->period_size*sizeof(ffado_sample_t));
	
	/* the scratch buffer is a buffer of one period that can be used as dummy memory */
	driver->scratchbuffer = calloc(driver->period_size, sizeof(ffado_sample_t));
	if(driver->scratchbuffer == NULL) {
		printError("could not allocate memory for scratch buffer");
		return -1;
	}
	
	/* packetizer thread options */
	driver->device_options.realtime=(driver->engine->control->real_time? 1 : 0);
	
	driver->device_options.packetizer_priority = driver->engine->rtpriority;
	if (driver->device_options.packetizer_priority > 98) {
		driver->device_options.packetizer_priority = 98;
	}
	if (driver->device_options.packetizer_priority < 1) {
		driver->device_options.packetizer_priority = 1;
	}

	driver->dev = ffado_streaming_init(driver->device_info, driver->device_options);

	if(!driver->dev) {
		printError("Error creating FFADO streaming device");
		return -1;
	}

	if (driver->device_options.realtime) {
		printMessage("Streaming thread running with Realtime scheduling, priority %d",
		           driver->device_options.packetizer_priority);
	} else {
		printMessage("Streaming thread running without Realtime scheduling");
	}

	ffado_streaming_set_audio_datatype(driver->dev, ffado_audio_datatype_float);
	
	/* ports */
	port_flags = JackPortIsOutput|JackPortIsPhysical|JackPortIsTerminal;

	driver->capture_nchannels=ffado_streaming_get_nb_capture_streams(driver->dev);
	driver->capture_channels=calloc(driver->capture_nchannels, sizeof(ffado_capture_channel_t));
	if(driver->capture_channels==NULL) {
		printError("could not allocate memory for capture channel list");
		return -1;
	}
	
	for (chn = 0; chn < driver->capture_nchannels; chn++) {
		ffado_streaming_get_capture_stream_name(driver->dev, chn, buf, sizeof(buf) - 1);
		driver->capture_channels[chn].stream_type=ffado_streaming_get_capture_stream_type(driver->dev, chn);

		if(driver->capture_channels[chn].stream_type == ffado_stream_type_audio) {
			snprintf(buf2, 64, "C%d_%s",(int)chn,buf); // needed to avoid duplicate names
			printMessage ("Registering audio capture port %s", buf2);
			if ((port = jack_port_register (driver->client, buf2,
							JACK_DEFAULT_AUDIO_TYPE,
							port_flags, 0)) == NULL) {
				printError (" cannot register port for %s", buf2);
				break;
			}
			driver->capture_ports =
				jack_slist_append (driver->capture_ports, port);
			// setup port parameters
			if (ffado_streaming_set_capture_stream_buffer(driver->dev, chn, NULL)) {
				printError(" cannot configure initial port buffer for %s", buf2);
			}
			if(ffado_streaming_capture_stream_onoff(driver->dev, chn, 1)) {
				printError(" cannot enable port %s", buf2);
			}
		} else if(driver->capture_channels[chn].stream_type == ffado_stream_type_midi) {
			snprintf(buf2, 64, "C%d_%s",(int)chn,buf); // needed to avoid duplicate names
			printMessage ("Registering midi capture port %s", buf2);
			if ((port = jack_port_register (driver->client, buf2,
							JACK_DEFAULT_MIDI_TYPE,
							port_flags, 0)) == NULL) {
				printError (" cannot register port for %s", buf2);
				break;
			}
			driver->capture_ports =
				jack_slist_append (driver->capture_ports, port);
			// setup port parameters
			if (ffado_streaming_set_capture_stream_buffer(driver->dev, chn, NULL)) {
				printError(" cannot configure initial port buffer for %s", buf2);
			}
			if(ffado_streaming_capture_stream_onoff(driver->dev, chn, 1)) {
				printError(" cannot enable port %s", buf2);
			}
			// setup midi unpacker
			midi_unpack_init(&driver->capture_channels[chn].midi_unpack);
			midi_unpack_reset(&driver->capture_channels[chn].midi_unpack);
			// setup the midi buffer
			driver->capture_channels[chn].midi_buffer = calloc(driver->period_size, sizeof(uint32_t));
		} else {
			printMessage ("Don't register capture port %s", buf);

			// we have to add a NULL entry in the list to be able to loop over the channels in the read/write routines
			driver->capture_ports =
				jack_slist_append (driver->capture_ports, NULL);
		}
		jack_port_set_latency (port, driver->period_size + driver->capture_frame_latency);
	}
	
	port_flags = JackPortIsInput|JackPortIsPhysical|JackPortIsTerminal;

	driver->playback_nchannels=ffado_streaming_get_nb_playback_streams(driver->dev);
	driver->playback_channels=calloc(driver->playback_nchannels, sizeof(ffado_playback_channel_t));
	if(driver->playback_channels==NULL) {
		printError("could not allocate memory for playback channel list");
		return -1;
	}

	for (chn = 0; chn < driver->playback_nchannels; chn++) {
		ffado_streaming_get_playback_stream_name(driver->dev, chn, buf, sizeof(buf) - 1);
		driver->playback_channels[chn].stream_type=ffado_streaming_get_playback_stream_type(driver->dev, chn);
		
		if(driver->playback_channels[chn].stream_type == ffado_stream_type_audio) {
			snprintf(buf2, 64, "P%d_%s",(int)chn,buf); // needed to avoid duplicate names
			printMessage ("Registering audio playback port %s", buf2);
			if ((port = jack_port_register (driver->client, buf2,
							JACK_DEFAULT_AUDIO_TYPE,
							port_flags, 0)) == NULL) {
				printError(" cannot register port for %s", buf2);
				break;
			}
			driver->playback_ports =
				jack_slist_append (driver->playback_ports, port);

			// setup port parameters
			if (ffado_streaming_set_playback_stream_buffer(driver->dev, chn, NULL)) {
				printError(" cannot configure initial port buffer for %s", buf2);
			}
			if(ffado_streaming_playback_stream_onoff(driver->dev, chn, 1)) {
				printError(" cannot enable port %s", buf2);
			}
		} else if(driver->playback_channels[chn].stream_type == ffado_stream_type_midi) {
			snprintf(buf2, 64, "P%d_%s",(int)chn,buf); // needed to avoid duplicate names
			printMessage ("Registering midi playback port %s", buf2);
			if ((port = jack_port_register (driver->client, buf2,
							JACK_DEFAULT_MIDI_TYPE,
							port_flags, 0)) == NULL) {
				printError(" cannot register port for %s", buf2);
				break;
			}
			driver->playback_ports =
				jack_slist_append (driver->playback_ports, port);

			// setup port parameters
			if (ffado_streaming_set_playback_stream_buffer(driver->dev, chn, NULL)) {
				printError(" cannot configure initial port buffer for %s", buf2);
			}
			if(ffado_streaming_playback_stream_onoff(driver->dev, chn, 1)) {
				printError(" cannot enable port %s", buf2);
			}
			// setup midi packer
			midi_pack_reset(&driver->playback_channels[chn].midi_pack);
			// setup the midi buffer
			driver->playback_channels[chn].midi_buffer = calloc(driver->period_size, sizeof(uint32_t));
		} else {
			printMessage ("Don't register playback port %s", buf);

			// we have to add a NULL entry in the list to be able to loop over the channels in the read/write routines
			driver->playback_ports =
				jack_slist_append (driver->playback_ports, NULL);
		}
		jack_port_set_latency (port, (driver->period_size * (driver->device_options.nb_buffers - 1)) + driver->playback_frame_latency);
	}

	if(ffado_streaming_prepare(driver->dev)) {
		printError("Could not prepare streaming device!");
		return -1;
	}

	return jack_activate (driver->client);
}

static int 
ffado_driver_detach (ffado_driver_t *driver)
{
	JSList *node;
	unsigned int chn;

	if (driver->engine == NULL) {
		return 0;
	}

	for (node = driver->capture_ports; node;
	     node = jack_slist_next (node)) {
		// Don't try to unregister NULL entries added for non-audio
		// ffado ports by ffado_driver_attach().
		if (node->data != NULL) {
			jack_port_unregister (driver->client,
					      ((jack_port_t *) node->data));
		}
	}

	jack_slist_free (driver->capture_ports);
	driver->capture_ports = 0;
		
	for (node = driver->playback_ports; node;
	     node = jack_slist_next (node)) {
        if (node->data != NULL) {
    		jack_port_unregister (driver->client,
				      ((jack_port_t *) node->data));
        }
	}

	jack_slist_free (driver->playback_ports);
	driver->playback_ports = 0;

	ffado_streaming_finish(driver->dev);
	driver->dev=NULL;

	for (chn = 0; chn < driver->capture_nchannels; chn++) {
		if(driver->capture_channels[chn].midi_buffer)
			free(driver->capture_channels[chn].midi_buffer);
	}
	free(driver->capture_channels);
	
	for (chn = 0; chn < driver->playback_nchannels; chn++) {
		if(driver->playback_channels[chn].midi_buffer)
			free(driver->playback_channels[chn].midi_buffer);
	}
	free(driver->playback_channels);
	
	free(driver->nullbuffer);
	free(driver->scratchbuffer);
	return 0;
}

static int
ffado_driver_read (ffado_driver_t * driver, jack_nframes_t nframes)
{
	channel_t chn;
	int nb_connections;
	JSList *node;
	jack_port_t* port;
	
	printEnter();
	for (chn = 0, node = driver->capture_ports; node; node = jack_slist_next (node), chn++) {
		if(driver->capture_channels[chn].stream_type == ffado_stream_type_audio) {
			port = (jack_port_t *) node->data;
			nb_connections = jack_port_connected (port);

			/* if there are no connections, use the dummy buffer and disable the stream */
			if(nb_connections) {
				ffado_streaming_capture_stream_onoff(driver->dev, chn, 1);
				ffado_streaming_set_capture_stream_buffer(driver->dev, chn, (char *)(jack_port_get_buffer (port, nframes)));
			} else {
				ffado_streaming_capture_stream_onoff(driver->dev, chn, 0);
				ffado_streaming_set_capture_stream_buffer(driver->dev, chn, (char *)(driver->scratchbuffer));
			}
		} else if (driver->capture_channels[chn].stream_type == ffado_stream_type_midi) {
			port = (jack_port_t *) node->data;
			nb_connections = jack_port_connected (port);
			if(nb_connections) {
				ffado_streaming_capture_stream_onoff(driver->dev, chn, 1);
			} else {
				ffado_streaming_capture_stream_onoff(driver->dev, chn, 0);
			}
			/* always set a buffer */
			ffado_streaming_set_capture_stream_buffer(driver->dev, chn, 
			                                          (char *)(driver->capture_channels[chn].midi_buffer));
		} else { /* ensure a valid buffer */
			ffado_streaming_set_capture_stream_buffer(driver->dev, chn, (char *)(driver->scratchbuffer));
			ffado_streaming_capture_stream_onoff(driver->dev, chn, 0);
		}
	}

	/* now transfer the buffers */
	ffado_streaming_transfer_capture_buffers(driver->dev);

	/* process the midi data */
	for (chn = 0, node = driver->capture_ports; node; node = jack_slist_next (node), chn++) {
		if (driver->capture_channels[chn].stream_type == ffado_stream_type_midi) {
			jack_default_audio_sample_t* buf;
			int i;
			int done;
			uint32_t *midi_buffer = driver->capture_channels[chn].midi_buffer;
			midi_unpack_t *midi_unpack = &driver->capture_channels[chn].midi_unpack;
			port = (jack_port_t *) node->data;
			nb_connections = jack_port_connected (port);
			buf = jack_port_get_buffer (port, nframes);

			/* if the returned buffer is invalid, discard the midi data */
			jack_midi_clear_buffer(buf);

			/* no connections means no processing */
			if(nb_connections == 0) continue;

			/* else unpack
			   note that libffado guarantees that midi bytes are on 8-byte aligned indexes */
			for(i = 0; i < nframes; i += 8) {
				if(midi_buffer[i] & 0xFF000000) {
					done = midi_unpack_buf(midi_unpack, (unsigned char *)(midi_buffer+i), 1, buf, i);
					if (done != 1) {
						printError("MIDI buffer overflow in channel %d\n", chn);
						break;
					}
				}
			}
		}
	}

	printExit();
	return 0;
}

static int
ffado_driver_write (ffado_driver_t * driver, jack_nframes_t nframes)
{
	channel_t chn;
	int nb_connections;
	JSList *node;
	jack_port_t *port;
	printEnter();
	
	driver->process_count++;
	if (driver->engine->freewheeling) {
		return 0;
	}

	for (chn = 0, node = driver->playback_ports; node; node = jack_slist_next (node), chn++) {
		if(driver->playback_channels[chn].stream_type == ffado_stream_type_audio) {
			port = (jack_port_t *) node->data;
			nb_connections = jack_port_connected (port);

			/* use the silent buffer + disable if there are no connections */
			if(nb_connections) {
				ffado_streaming_playback_stream_onoff(driver->dev, chn, 1);
				ffado_streaming_set_playback_stream_buffer(driver->dev, chn, (char *)(jack_port_get_buffer (port, nframes)));
			} else {
				ffado_streaming_playback_stream_onoff(driver->dev, chn, 0);
				ffado_streaming_set_playback_stream_buffer(driver->dev, chn, (char *)(driver->nullbuffer));
			}
		} else if (driver->playback_channels[chn].stream_type == ffado_stream_type_midi) {
			jack_default_audio_sample_t* buf;
			int nevents;
			int i;
			midi_pack_t *midi_pack = &driver->playback_channels[chn].midi_pack;
			uint32_t *midi_buffer = driver->playback_channels[chn].midi_buffer;
			int min_next_pos = 0;

			port = (jack_port_t *) node->data;
			nb_connections = jack_port_connected (port);

			/* skip if no connections */
			if(nb_connections == 0) {
				ffado_streaming_set_playback_stream_buffer(driver->dev, chn, (char *)(driver->nullbuffer));
				ffado_streaming_playback_stream_onoff(driver->dev, chn, 0);
				continue;
			}

			memset(midi_buffer, 0, nframes * sizeof(uint32_t));
			ffado_streaming_set_playback_stream_buffer(driver->dev, chn, (char *)(midi_buffer));
			ffado_streaming_playback_stream_onoff(driver->dev, chn, 1);

			/* check if we still have to process bytes from the previous period */
			/*
			if(driver->playback_channels[chn].nb_overflow_bytes) {
				printMessage("have to process %d bytes from previous period", driver->playback_channels[chn].nb_overflow_bytes);
			}
			*/
			for (i=0; i<driver->playback_channels[chn].nb_overflow_bytes; ++i) {
				midi_buffer[min_next_pos] = 0x01000000 | (driver->playback_channels[chn].overflow_buffer[i] & 0xFF);
				min_next_pos += 8;
			}
			driver->playback_channels[chn].nb_overflow_bytes=0;
			
			/* process the events in this period */
			buf = jack_port_get_buffer (port, nframes);
			nevents = jack_midi_get_event_count(buf);

			for (i=0; i<nevents; ++i) {
				int j;
				jack_midi_event_t event;
				jack_midi_event_get(&event, buf, i);

				midi_pack_event(midi_pack, &event);

				/* floor the initial position to be a multiple of 8 */
				int pos = event.time & 0xFFFFFFF8;
				for(j = 0; j < event.size; j++) {
					/* make sure we don't overwrite a previous byte */
					while(pos < min_next_pos && pos < nframes) {
						/* printMessage("have to correct pos to %d", pos); */
						pos += 8;
					}

					if(pos >= nframes) {
						int f;
						/* printMessage("midi message crosses period boundary"); */
						driver->playback_channels[chn].nb_overflow_bytes = event.size - j;
						if(driver->playback_channels[chn].nb_overflow_bytes > MIDI_OVERFLOW_BUFFER_SIZE) {
							printError("too much midi bytes cross period boundary");
							driver->playback_channels[chn].nb_overflow_bytes = MIDI_OVERFLOW_BUFFER_SIZE;
						}
						/* save the bytes that still have to be transmitted in the next period */
						for(f = 0; f < driver->playback_channels[chn].nb_overflow_bytes; f++) {
							driver->playback_channels[chn].overflow_buffer[f] = event.buffer[j+f];
						}
						/* exit since we can't transmit anything anymore.
						   the rate should be controlled */
						if(i < nevents-1) {
							printError("%d midi events lost due to period crossing", nevents-i-1);
						}
						break;
					} else {
						midi_buffer[pos] = 0x01000000 | (event.buffer[j] & 0xFF);
						pos += 8;
						min_next_pos = pos;
					}
				}
			}
		} else { /* ensure a valid buffer */
			ffado_streaming_set_playback_stream_buffer(driver->dev, chn, (char *)(driver->nullbuffer));
			ffado_streaming_playback_stream_onoff(driver->dev, chn, 0);
		}
	}

	ffado_streaming_transfer_playback_buffers(driver->dev);

	printExit();
	return 0;
}

//static inline jack_nframes_t 
static jack_nframes_t 
ffado_driver_wait (ffado_driver_t *driver, int extra_fd, int *status,
		   float *delayed_usecs)
{
	jack_time_t                   wait_enter;
	jack_time_t                   wait_ret;
	ffado_wait_response           response;
	
	printEnter();

	wait_enter = jack_get_microseconds ();
	if (wait_enter > driver->wait_next) {
		/*
			* This processing cycle was delayed past the
			* next due interrupt!  Do not account this as
			* a wakeup delay:
			*/
		driver->wait_next = 0;
		driver->wait_late++;
	}
// *status = -2; interrupt
// *status = -3; timeout
// *status = -4; extra FD

	response = ffado_streaming_wait(driver->dev);
	
	wait_ret = jack_get_microseconds ();
	
	if (driver->wait_next && wait_ret > driver->wait_next) {
		*delayed_usecs = wait_ret - driver->wait_next;
	}
	driver->wait_last = wait_ret;
	driver->wait_next = wait_ret + driver->period_usecs;
	driver->engine->transport_cycle_start (driver->engine, wait_ret);
	if (response == ffado_wait_ok) {
		// all good
		*status=0;
	} else if (response == ffado_wait_xrun) {
		// xrun happened, but it's handled
		*status=0;
		return 0;
	} else if (response == ffado_wait_error) {
		// an error happened (unhandled xrun)
		// this should be fatal
		*status=-1;
		return 0;
	} else if (response == ffado_wait_shutdown) {
		// we are to shutdown the ffado system
		// this should be fatal
		*status=-1;
		return 0;
	} else {
		// we don't know about this response code
		printError("unknown wait response (%d) from ffado", response);
		// this should be fatal
		*status=-1;
		return 0;
	}

	driver->last_wait_ust = wait_ret;

	// FIXME: this should do something more useful
	*delayed_usecs = 0;
	
	printExit();

	return driver->period_size;
}

static int
ffado_driver_run_cycle (ffado_driver_t *driver)
{
	jack_engine_t *engine = driver->engine;
	int wait_status=0;
	float delayed_usecs=0.0;

	jack_nframes_t nframes = ffado_driver_wait (driver, -1,
	   &wait_status, &delayed_usecs);
	
	if ((wait_status < 0)) {
		printError( "wait status < 0! (= %d)",wait_status);
		return -1;
	}
		
	if ((nframes == 0)) {
		/* we detected an xrun and restarted: notify
		 * clients about the delay. */
		printMessage("xrun detected");
		engine->delay (engine, delayed_usecs);
		return 0;
	} 
	
	return engine->run_cycle (engine, nframes, delayed_usecs);

}
/*
 * in a null cycle we should discard the input and write silence to the outputs
 */
static int
ffado_driver_null_cycle (ffado_driver_t* driver, jack_nframes_t nframes)
{
	channel_t chn;
	JSList *node;
	ffado_streaming_stream_type stream_type;

	printEnter();
	
	if (driver->engine->freewheeling) {
		return 0;
	}

	// write silence to buffer
	for (chn = 0, node = driver->playback_ports; node; node = jack_slist_next (node), chn++) {
		stream_type=ffado_streaming_get_playback_stream_type(driver->dev, chn);

		if(stream_type == ffado_stream_type_audio) {
			ffado_streaming_set_playback_stream_buffer(driver->dev, chn, (char *)(driver->nullbuffer));
		}
	}
	ffado_streaming_transfer_playback_buffers(driver->dev);
	
	// read & discard from input ports
	for (chn = 0, node = driver->capture_ports; node; node = jack_slist_next (node), chn++) {
		stream_type=ffado_streaming_get_capture_stream_type(driver->dev, chn);
		if(stream_type == ffado_stream_type_audio) {
			ffado_streaming_set_capture_stream_buffer(driver->dev, chn, (char *)(driver->scratchbuffer));
		}
	}
	ffado_streaming_transfer_capture_buffers(driver->dev);

	printExit();
	return 0;
}

static int
ffado_driver_start (ffado_driver_t *driver)
{
	int retval=0;

	if((retval=ffado_streaming_start(driver->dev))) {
		printError("Could not start streaming threads: %d", retval);
		return retval;
	}

	return 0;

}

static int
ffado_driver_stop (ffado_driver_t *driver)
{
	int retval=0;
	
	if((retval=ffado_streaming_stop(driver->dev))) {
		printError("Could not stop streaming threads");
		return retval;
	}

	return 0;
}


static int
ffado_driver_bufsize (ffado_driver_t* driver, jack_nframes_t nframes)
{
	printError("Buffer size change requested but not supported!!!");

	/*
	 driver->period_size = nframes;  
	driver->period_usecs =
		(jack_time_t) floor ((((float) nframes) / driver->sample_rate)
				     * 1000000.0f);
	*/
	
	/* tell the engine to change its buffer size */
#if 0
	if (driver->engine->set_buffer_size (driver->engine, nframes)) {
		jack_error ("FFADO: cannot set engine buffer size to %d (check MIDI)", nframes);
		return -1;
	}
#endif

	return -1; // unsupported
}

typedef void (*JackDriverFinishFunction) (jack_driver_t *);

ffado_driver_t *
ffado_driver_new (jack_client_t * client,
		  char *name,
		  ffado_jack_settings_t *params)
{
	ffado_driver_t *driver;

	if(ffado_get_api_version() != FIREWIRE_REQUIRED_FFADO_API_VERSION) {
		printError("Incompatible libffado version! (%s)", ffado_get_version());
		return NULL;
	}

	printMessage("Starting firewire backend (%s)", ffado_get_version());

	driver = calloc (1, sizeof (ffado_driver_t));

	/* Setup the jack interfaces */  
	jack_driver_nt_init ((jack_driver_nt_t *) driver);

	driver->nt_attach    = (JackDriverNTAttachFunction)   ffado_driver_attach;
	driver->nt_detach    = (JackDriverNTDetachFunction)   ffado_driver_detach;
	driver->nt_start     = (JackDriverNTStartFunction)    ffado_driver_start;
	driver->nt_stop      = (JackDriverNTStopFunction)     ffado_driver_stop;
	driver->nt_run_cycle = (JackDriverNTRunCycleFunction) ffado_driver_run_cycle;
	driver->null_cycle   = (JackDriverNullCycleFunction)  ffado_driver_null_cycle;
	driver->write        = (JackDriverReadFunction)       ffado_driver_write;
	driver->read         = (JackDriverReadFunction)       ffado_driver_read;
	driver->nt_bufsize   = (JackDriverNTBufSizeFunction)  ffado_driver_bufsize;
	
	/* copy command line parameter contents to the driver structure */
	memcpy(&driver->settings,params,sizeof(ffado_jack_settings_t));
	
	/* prepare all parameters */
	driver->sample_rate = params->sample_rate;
	driver->period_size = params->period_size;
	driver->last_wait_ust = 0;
	
	driver->period_usecs =
		(jack_time_t) floor ((((float) driver->period_size) * 1000000.0f) / driver->sample_rate);

	driver->client = client;
	driver->engine = NULL;

	memset(&driver->device_options,0,sizeof(driver->device_options));	
	driver->device_options.sample_rate=params->sample_rate;
	driver->device_options.period_size=params->period_size;
	driver->device_options.nb_buffers=params->buffer_size;
	driver->capture_frame_latency = params->capture_frame_latency;
	driver->playback_frame_latency = params->playback_frame_latency;
	driver->device_options.slave_mode=params->slave_mode;
	driver->device_options.snoop_mode=params->snoop_mode;
	driver->device_options.verbose=params->verbose_level;
	
	driver->device_info.nb_device_spec_strings=1;
	driver->device_info.device_spec_strings=calloc(1, sizeof(char *));
	driver->device_info.device_spec_strings[0]=strdup(params->device_info);
	
	debugPrint(DEBUG_LEVEL_STARTUP, " Driver compiled on %s %s for FFADO %s (API version %d)",
	                                __DATE__, __TIME__, ffado_get_version(), ffado_get_api_version());
	debugPrint(DEBUG_LEVEL_STARTUP, " Created driver %s", name);
	debugPrint(DEBUG_LEVEL_STARTUP, "            period_size: %d", driver->period_size);
	debugPrint(DEBUG_LEVEL_STARTUP, "            period_usecs: %d", driver->period_usecs);
	debugPrint(DEBUG_LEVEL_STARTUP, "            sample rate: %d", driver->sample_rate);

	return (ffado_driver_t *) driver;

}

static void
ffado_driver_delete (ffado_driver_t *driver)
{
	unsigned int i;
	jack_driver_nt_finish ((jack_driver_nt_t *) driver);
	for (i=0; i < driver->device_info.nb_device_spec_strings; i++) {
		free (driver->device_info.device_spec_strings[i]);
	}
	free (driver->device_info.device_spec_strings);
	free (driver);
}

/*
 * dlopen plugin stuff
 */

const char driver_client_name[] = "firewire_pcm";

const jack_driver_desc_t *
driver_get_descriptor ()
{
	jack_driver_desc_t * desc;
	jack_driver_param_desc_t * params;
	unsigned int i;

	desc = calloc (1, sizeof (jack_driver_desc_t));

	strcpy (desc->name, "firewire");
	desc->nparams = 11;
  
	params = calloc (desc->nparams, sizeof (jack_driver_param_desc_t));
	desc->params = params;

	i = 0;
	strcpy (params[i].name, "device");
	params[i].character  = 'd';
	params[i].type       = JackDriverParamString;
	strcpy (params[i].value.str,  "hw:0");
	strcpy (params[i].short_desc, "The FireWire device to use.");
	strcpy (params[i].long_desc,  "The FireWire device to use. Please consult the FFADO documentation for more info.");
	
	i++;
	strcpy (params[i].name, "period");
	params[i].character  = 'p';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 1024;
	strcpy (params[i].short_desc, "Frames per period");
	strcpy (params[i].long_desc, params[i].short_desc);
	
	i++;
	strcpy (params[i].name, "nperiods");
	params[i].character  = 'n';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 3;
	strcpy (params[i].short_desc, "Number of periods of playback latency");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "rate");
	params[i].character  = 'r';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 48000U;
	strcpy (params[i].short_desc, "Sample rate");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "capture");
	params[i].character  = 'i';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 1U;
	strcpy (params[i].short_desc, "Provide capture ports.");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "playback");
	params[i].character  = 'o';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 1U;
	strcpy (params[i].short_desc, "Provide playback ports.");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "input-latency");
	params[i].character  = 'I';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui    = 0;
	strcpy (params[i].short_desc, "Extra input latency (frames)");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "output-latency");
	params[i].character  = 'O';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui    = 0;
	strcpy (params[i].short_desc, "Extra output latency (frames)");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "slave");
	params[i].character  = 'x';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 0U;
	strcpy (params[i].short_desc, "Act as a BounceDevice slave");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "slave");
	params[i].character  = 'X';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 0U;
	strcpy (params[i].short_desc, "Operate in snoop mode");
	strcpy (params[i].long_desc, params[i].short_desc);
	
	i++;
	strcpy (params[i].name, "verbose");
	params[i].character  = 'v';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 0U;
	strcpy (params[i].short_desc, "Verbose level for the firewire backend");
	strcpy (params[i].long_desc, params[i].short_desc);

	return desc;
}


jack_driver_t *
driver_initialize (jack_client_t *client, JSList * params)
{
	jack_driver_t *driver;

	const JSList * node;
	const jack_driver_param_t * param;

	ffado_jack_settings_t cmlparams;

	char *device_name="hw:0"; 

	cmlparams.period_size_set=0;
	cmlparams.sample_rate_set=0;
	cmlparams.buffer_size_set=0;

	/* default values */
	cmlparams.period_size=1024;
	cmlparams.sample_rate=48000;
	cmlparams.buffer_size=3;
	cmlparams.playback_ports=1;
	cmlparams.capture_ports=1;
	cmlparams.playback_frame_latency=0;
	cmlparams.capture_frame_latency=0;
	cmlparams.slave_mode=0;
	cmlparams.snoop_mode=0;
	cmlparams.verbose_level=0;

	for (node = params; node; node = jack_slist_next (node))
	{
		param = (jack_driver_param_t *) node->data;

		switch (param->character)
		{
		case 'd':
			device_name = strdup (param->value.str);
			break;
		case 'p':
			cmlparams.period_size = param->value.ui;
			cmlparams.period_size_set = 1;
			break;
		case 'n':
			cmlparams.buffer_size = param->value.ui;
			cmlparams.buffer_size_set = 1;
			break;
		case 'r':
			cmlparams.sample_rate = param->value.ui;
			cmlparams.sample_rate_set = 1;
			break;
		case 'i':
			cmlparams.capture_ports = param->value.ui;
			break;
		case 'o':
			cmlparams.playback_ports = param->value.ui;
			break;
		case 'I':
			cmlparams.capture_frame_latency = param->value.ui;
			break;
		case 'O':
			cmlparams.playback_frame_latency = param->value.ui;
			break;
		case 'x':
			cmlparams.slave_mode = param->value.ui;
			break;
		case 'X':
			cmlparams.snoop_mode = param->value.ui;
			break;
		case 'v':
			cmlparams.verbose_level = param->value.ui;
			break;
		}
	}

        // temporary
        cmlparams.device_info = device_name;

	driver=(jack_driver_t *)ffado_driver_new (client, "ffado_pcm", &cmlparams);

	return driver;
}

void
driver_finish (jack_driver_t *driver)
{
	ffado_driver_t *drv=(ffado_driver_t *) driver;
	// If jack hasn't called the detach method, do it now.  As of jack 0.101.1
	// the detach method was not being called explicitly on closedown, and 
	// we need it to at least deallocate the iso resources.
	if (drv->dev != NULL)
		ffado_driver_detach(drv);
	ffado_driver_delete (drv);
}
