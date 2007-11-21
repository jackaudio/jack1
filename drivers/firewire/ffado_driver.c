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

#include "ffado_driver.h"

#define SAMPLE_MAX_24BIT  8388608.0f
#define SAMPLE_MAX_16BIT  32768.0f

static int ffado_driver_stop (ffado_driver_t *driver);

#ifdef FFADO_DRIVER_WITH_ASEQ_MIDI
	static ffado_driver_midi_handle_t *ffado_driver_midi_init(ffado_driver_t *driver);
	static void ffado_driver_midi_finish (ffado_driver_midi_handle_t *m);
	static int ffado_driver_midi_start (ffado_driver_midi_handle_t *m);
	static int ffado_driver_midi_stop (ffado_driver_midi_handle_t *m);
#endif

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
	driver->device_options.verbose=g_verbose;

	driver->engine->set_buffer_size (driver->engine, driver->period_size);
	driver->engine->set_sample_rate (driver->engine, driver->sample_rate);

	/* packetizer thread options */
	driver->device_options.realtime=(driver->engine->control->real_time? 1 : 0);
	
	driver->device_options.packetizer_priority=driver->engine->control->client_priority +
		FFADO_RT_PRIORITY_PACKETIZER_RELATIVE;
	if (driver->device_options.packetizer_priority>98) {
		driver->device_options.packetizer_priority=98;
	}

	driver->dev=ffado_streaming_init(&driver->device_info,driver->device_options);

	if(!driver->dev) {
		printError("Error creating FFADO streaming device");
		return -1;
	}

#ifdef FFADO_DRIVER_WITH_ASEQ_MIDI
	driver->midi_handle=ffado_driver_midi_init(driver);
	if(!driver->midi_handle) {
		printError("-----------------------------------------------------------");
		printError("Error creating midi device!");
		printError("The FireWire backend will run without MIDI support.");
		printError("Consult the above error messages to solve the problem. ");
		printError("-----------------------------------------------------------\n\n");
	}
#endif

	if (driver->device_options.realtime) {
		printMessage("Streaming thread running with Realtime scheduling, priority %d",
		           driver->device_options.packetizer_priority);
	} else {
		printMessage("Streaming thread running without Realtime scheduling");
	}

	/* ports */
	port_flags = JackPortIsOutput|JackPortIsPhysical|JackPortIsTerminal;

	driver->capture_nchannels=ffado_streaming_get_nb_capture_streams(driver->dev);

	for (chn = 0; chn < driver->capture_nchannels; chn++) {
		
		ffado_streaming_get_capture_stream_name(driver->dev, chn, buf, sizeof(buf) - 1);
		
		if(ffado_streaming_get_capture_stream_type(driver->dev, chn) != ffado_stream_type_audio) {
			printMessage ("Don't register capture port %s", buf);

			// we have to add a NULL entry in the list to be able to loop over the channels in the read/write routines
			driver->capture_ports =
				jack_slist_append (driver->capture_ports, NULL);
		} else {
			snprintf(buf2, 64, "C%d_%s",(int)chn,buf); // needed to avoid duplicate names
			printMessage ("Registering capture port %s", buf2);
			if ((port = jack_port_register (driver->client, buf2,
							JACK_DEFAULT_AUDIO_TYPE,
							port_flags, 0)) == NULL) {
				printError (" cannot register port for %s", buf2);
				break;
			}
			driver->capture_ports =
				jack_slist_append (driver->capture_ports, port);
			// setup port parameters
			if(ffado_streaming_set_capture_buffer_type(driver->dev, chn, ffado_buffer_type_float)) {
				printError(" cannot set port buffer type for %s", buf2);
			}
			if (ffado_streaming_set_capture_stream_buffer(driver->dev, chn, NULL)) {
				printError(" cannot configure initial port buffer for %s", buf2);
			}
			if(ffado_streaming_capture_stream_onoff(driver->dev, chn, 1)) {
				printError(" cannot enable port %s", buf2);
			}
		}
		jack_port_set_latency (port, driver->period_size + driver->capture_frame_latency);

	}
	
	port_flags = JackPortIsInput|JackPortIsPhysical|JackPortIsTerminal;

	driver->playback_nchannels=ffado_streaming_get_nb_playback_streams(driver->dev);

	for (chn = 0; chn < driver->playback_nchannels; chn++) {

		ffado_streaming_get_playback_stream_name(driver->dev, chn, buf, sizeof(buf) - 1);
		
		if(ffado_streaming_get_playback_stream_type(driver->dev, chn) != ffado_stream_type_audio) {
			printMessage ("Don't register playback port %s", buf);

			// we have to add a NULL entry in the list to be able to loop over the channels in the read/write routines
			driver->playback_ports =
				jack_slist_append (driver->playback_ports, NULL);
		} else {
			snprintf(buf2, 64, "P%d_%s",(int)chn,buf); // needed to avoid duplicate names
			printMessage ("Registering playback port %s", buf2);
			if ((port = jack_port_register (driver->client, buf2,
							JACK_DEFAULT_AUDIO_TYPE,
							port_flags, 0)) == NULL) {
				printError(" cannot register port for %s", buf2);
				break;
			}
			driver->playback_ports =
				jack_slist_append (driver->playback_ports, port);

			// setup port parameters
			if(ffado_streaming_set_playback_buffer_type(driver->dev, chn, ffado_buffer_type_float)) {
				printError(" cannot set port buffer type for %s", buf2);
			}
			if (ffado_streaming_set_playback_stream_buffer(driver->dev, chn, NULL)) {
				printError(" cannot configure initial port buffer for %s", buf2);
			}
			if(ffado_streaming_playback_stream_onoff(driver->dev, chn, 1)) {
				printError(" cannot enable port %s", buf2);
			}
		}
		jack_port_set_latency (port, (driver->period_size * (driver->device_options.nb_buffers - 1)) + driver->playback_frame_latency);

	}

	if(!ffado_streaming_prepare(driver->dev)) {
		printError("Could not prepare streaming device!");
		return -1;
	}
	

	return jack_activate (driver->client);
}

static int 
ffado_driver_detach (ffado_driver_t *driver)
{
	JSList *node;

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

#ifdef FFADO_DRIVER_WITH_ASEQ_MIDI
	if(driver->midi_handle) {
		ffado_driver_midi_finish(driver->midi_handle);	
	}
	driver->midi_handle=NULL;
#endif

	return 0;
}

static inline void 
ffado_driver_read_from_channel (ffado_driver_t *driver,
			       channel_t channel,
			       jack_default_audio_sample_t *dst,
			       jack_nframes_t nsamples)
{
	
	ffado_sample_t buffer[nsamples];
	char *src=(char *)buffer;
	
	ffado_streaming_read(driver->dev, channel, buffer, nsamples);
	
	/* ALERT: signed sign-extension portability !!! */

	while (nsamples--) {
		int x;
#if __BYTE_ORDER == __LITTLE_ENDIAN
		memcpy((char*)&x + 1, src, 3);
#elif __BYTE_ORDER == __BIG_ENDIAN
		memcpy(&x, src, 3);
#endif
		x >>= 8;
		*dst = x / SAMPLE_MAX_24BIT;
		dst++;
		src += sizeof(ffado_sample_t);
	}
	
}

static int
ffado_driver_read (ffado_driver_t * driver, jack_nframes_t nframes)
{
	jack_default_audio_sample_t* buf;
	channel_t chn;
	JSList *node;
	jack_port_t* port;
	
	ffado_sample_t nullbuffer[nframes];
	void *addr_of_nullbuffer=(void *)nullbuffer;

	ffado_streaming_stream_type stream_type;
	
	printEnter();
	
	for (chn = 0, node = driver->capture_ports; node; node = jack_slist_next (node), chn++) {
		stream_type = ffado_streaming_get_capture_stream_type(driver->dev, chn);
		if(stream_type == ffado_stream_type_audio) {
			port = (jack_port_t *) node->data;

			buf = jack_port_get_buffer (port, nframes);
			if(!buf) buf=(jack_default_audio_sample_t*)addr_of_nullbuffer;
				
			ffado_streaming_set_capture_stream_buffer(driver->dev, chn, (char *)(buf));
		}
	}

	// now transfer the buffers
	ffado_streaming_transfer_capture_buffers(driver->dev);
	
	printExit();
	
	return 0;

}

static inline void 
ffado_driver_write_to_channel (ffado_driver_t *driver,
			      channel_t channel, 
			      jack_default_audio_sample_t *buf, 
			      jack_nframes_t nsamples)
{
    long long y;
	ffado_sample_t buffer[nsamples];
	unsigned int i=0;	
    char *dst=(char *)buffer;
	
	// convert from float to integer
	for(;i<nsamples;i++) {
		y = (long long)(*buf * SAMPLE_MAX_24BIT);

		if (y > (INT_MAX >> 8 )) {
			y = (INT_MAX >> 8);
		} else if (y < (INT_MIN >> 8 )) {
			y = (INT_MIN >> 8 );
		}
#if __BYTE_ORDER == __LITTLE_ENDIAN
		memcpy (dst, &y, 3);
#elif __BYTE_ORDER == __BIG_ENDIAN
		memcpy (dst, (char *)&y + 5, 3);
#endif
		dst += sizeof(ffado_sample_t);
		buf++;
	}
	
	// write to the ffado streaming device
	ffado_streaming_write(driver->dev, channel, buffer, nsamples);
	
}

static int
ffado_driver_write (ffado_driver_t * driver, jack_nframes_t nframes)
{
	channel_t chn;
	JSList *node;
	jack_default_audio_sample_t* buf;

	jack_port_t *port;

	ffado_streaming_stream_type stream_type;

	ffado_sample_t nullbuffer[nframes];
	void *addr_of_nullbuffer = (void*)nullbuffer;

	memset(&nullbuffer,0,nframes*sizeof(ffado_sample_t));

	printEnter();

	driver->process_count++;

	assert(driver->dev);

 	if (driver->engine->freewheeling) {
 		return 0;
 	}

	for (chn = 0, node = driver->playback_ports; node; node = jack_slist_next (node), chn++) {
		stream_type=ffado_streaming_get_playback_stream_type(driver->dev, chn);
		if(stream_type == ffado_stream_type_audio) {
			port = (jack_port_t *) node->data;

			buf = jack_port_get_buffer (port, nframes);
			if(!buf) buf=(jack_default_audio_sample_t*)addr_of_nullbuffer;
				
			ffado_streaming_set_playback_stream_buffer(driver->dev, chn, (char *)(buf));
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
	int nframes;
	jack_time_t                   wait_enter;
	jack_time_t                   wait_ret;
	
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

	nframes=ffado_streaming_wait(driver->dev);
	
	wait_ret = jack_get_microseconds ();
	
	if (driver->wait_next && wait_ret > driver->wait_next) {
		*delayed_usecs = wait_ret - driver->wait_next;
	}
	driver->wait_last = wait_ret;
	driver->wait_next = wait_ret + driver->period_usecs;
	driver->engine->transport_cycle_start (driver->engine, wait_ret);
	
	// transfer the streaming buffers
	// we now do this in the read/write functions
// 	ffado_streaming_transfer_buffers(driver->dev);
	
	if (nframes < 0) {
		*status=0;
		
		return 0;
		//nframes=driver->period_size; //debug
	}

	*status = 0;
	driver->last_wait_ust = wait_ret;

	// FIXME: this should do something more usefull
	*delayed_usecs = 0;
	
	printExit();

	return nframes - nframes % driver->period_size;
	
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
	snd_pcm_sframes_t nwritten;

	ffado_streaming_stream_type stream_type;

	jack_default_audio_sample_t buff[nframes];
	jack_default_audio_sample_t* buffer=(jack_default_audio_sample_t*)buff;
	
	printEnter();

	memset(buffer,0,nframes*sizeof(jack_default_audio_sample_t));
	
	assert(driver->dev);

 	if (driver->engine->freewheeling) {
 		return 0;
 	}

	// write silence to buffer
	nwritten = 0;

	for (chn = 0, node = driver->playback_ports; node; node = jack_slist_next (node), chn++) {
		stream_type=ffado_streaming_get_playback_stream_type(driver->dev, chn);

		if(stream_type == ffado_stream_type_audio) {
			ffado_streaming_set_playback_stream_buffer(driver->dev, chn, (char *)(buffer));
		}
	}

	ffado_streaming_transfer_playback_buffers(driver->dev);
	
	// read & discard from input ports
	for (chn = 0, node = driver->capture_ports; node; node = jack_slist_next (node), chn++) {
		stream_type=ffado_streaming_get_capture_stream_type(driver->dev, chn);
		if(stream_type == ffado_stream_type_audio) {
			ffado_streaming_set_capture_stream_buffer(driver->dev, chn, (char *)(buffer));
		}
	}

	// now transfer the buffers
	ffado_streaming_transfer_capture_buffers(driver->dev);
		
	printExit();
	return 0;
}

static int
ffado_driver_start (ffado_driver_t *driver)
{
	int retval=0;

#ifdef FFADO_DRIVER_WITH_ASEQ_MIDI
	if(driver->midi_handle) {
		if((retval=ffado_driver_midi_start(driver->midi_handle))) {
			printError("Could not start MIDI threads");
			return retval;
		}
	}
#endif	

	if((retval=ffado_streaming_start(driver->dev))) {
		printError("Could not start streaming threads");
#ifdef FFADO_DRIVER_WITH_ASEQ_MIDI
		if(driver->midi_handle) {
			ffado_driver_midi_stop(driver->midi_handle);
		}
#endif	
		return retval;
	}

	return 0;

}

static int
ffado_driver_stop (ffado_driver_t *driver)
{
	int retval=0;
	
#ifdef FFADO_DRIVER_WITH_ASEQ_MIDI
	if(driver->midi_handle) {
		if((retval=ffado_driver_midi_stop(driver->midi_handle))) {
			printError("Could not stop MIDI threads");
			return retval;
		}
	}
#endif	
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
	//driver->engine->set_buffer_size (driver->engine, nframes);

	return -1; // unsupported
}

typedef void (*JackDriverFinishFunction) (jack_driver_t *);

ffado_driver_t *
ffado_driver_new (jack_client_t * client,
		  char *name,
		  ffado_jack_settings_t *params)
{
	ffado_driver_t *driver;

	assert(params);

	if(ffado_get_api_version() != 2) {
		printError("Incompatible libffado version! (%s)", ffado_get_version());
		return NULL;
	}

	printMessage("Starting Freebob backend (%s)", ffado_get_version());

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
	driver->device_options.node_id=params->node_id;
	driver->device_options.port=params->port;
	driver->capture_frame_latency = params->capture_frame_latency;
	driver->playback_frame_latency = params->playback_frame_latency;
	driver->device_options.slave_mode=params->slave_mode;
	driver->device_options.snoop_mode=params->snoop_mode;

	if(!params->capture_ports) {
		driver->device_options.directions |= FFADO_IGNORE_CAPTURE;
	}

	if(!params->playback_ports) {
		driver->device_options.directions |= FFADO_IGNORE_PLAYBACK;
	}

	debugPrint(DEBUG_LEVEL_STARTUP, " Driver compiled on %s %s", __DATE__, __TIME__);
	debugPrint(DEBUG_LEVEL_STARTUP, " Created driver %s", name);
	debugPrint(DEBUG_LEVEL_STARTUP, "            period_size: %d", driver->period_size);
	debugPrint(DEBUG_LEVEL_STARTUP, "            period_usecs: %d", driver->period_usecs);
	debugPrint(DEBUG_LEVEL_STARTUP, "            sample rate: %d", driver->sample_rate);

	return (ffado_driver_t *) driver;

}

static void
ffado_driver_delete (ffado_driver_t *driver)
{
	jack_driver_nt_finish ((jack_driver_nt_t *) driver);
	free (driver);
}

#ifdef FFADO_DRIVER_WITH_ASEQ_MIDI
/*
 * MIDI support
 */ 

// the thread that will queue the midi events from the seq to the stream buffers

void * ffado_driver_midi_queue_thread(void *arg)
{
	ffado_driver_midi_handle_t *m=(ffado_driver_midi_handle_t *)arg;
	assert(m);
	snd_seq_event_t *ev;
	unsigned char work_buffer[MIDI_TRANSMIT_BUFFER_SIZE];
	int bytes_to_send;
	int b;
	int i;

	printMessage("MIDI queue thread started");

	while(1) {
		// get next event, if one is present
		while ((snd_seq_event_input(m->seq_handle, &ev) > 0)) {
			// get the port this event is originated from
			ffado_midi_port_t *port=NULL;
			for (i=0;i<m->nb_output_ports;i++) {
				if(m->output_ports[i]->seq_port_nr == ev->dest.port) {
					port=m->output_ports[i];
					break;
				}
			}
	
			if(!port) {
				printError(" Could not find target port for event: dst=%d src=%d", ev->dest.port, ev->source.port);

				break;
			}
			
			// decode it to the work buffer
			if((bytes_to_send = snd_midi_event_decode ( port->parser, 
				work_buffer,
				MIDI_TRANSMIT_BUFFER_SIZE, 
				ev))<0) 
			{ // failed
				printError(" Error decoding event for port %d (errcode=%d)", port->seq_port_nr,bytes_to_send);
				bytes_to_send=0;
				//return -1;
			}
	
			for(b=0;b<bytes_to_send;b++) {
				ffado_sample_t tmp_event=work_buffer[b];
				if(ffado_streaming_write(m->dev, port->stream_nr, &tmp_event, 1)<1) {
					printError(" Midi send buffer overrun");
				}
			}
	
		}

		// sleep for some time
		usleep(MIDI_THREAD_SLEEP_TIME_USECS);
	}
	return NULL;
}

// the dequeue thread (maybe we need one thread per stream)
void *ffado_driver_midi_dequeue_thread (void *arg) {
	ffado_driver_midi_handle_t *m=(ffado_driver_midi_handle_t *)arg;

	int i;
	int s;
	
	int samples_read;

	assert(m);

	while(1) {
		// read incoming events
	
		for (i=0;i<m->nb_input_ports;i++) {
			unsigned int buff[64];
	
			ffado_midi_port_t *port=m->input_ports[i];
		
			if(!port) {
				printError(" something went wrong when setting up the midi input port map (%d)",i);
			}
		
			do {
				samples_read=ffado_streaming_read(m->dev, port->stream_nr, buff, 64);
			
				for (s=0;s<samples_read;s++) {
					unsigned int *byte=(buff+s) ;
					snd_seq_event_t ev;
					if ((snd_midi_event_encode_byte(port->parser,(*byte) & 0xFF, &ev)) > 0) {
						// a midi message is complete, send it out to ALSA
						snd_seq_ev_set_subs(&ev);  
						snd_seq_ev_set_direct(&ev);
						snd_seq_ev_set_source(&ev, port->seq_port_nr);
						snd_seq_event_output_direct(port->seq_handle, &ev);						
					}
				}
			} while (samples_read>0);
		}

		// sleep for some time
		usleep(MIDI_THREAD_SLEEP_TIME_USECS);
	}
	return NULL;
}

static ffado_driver_midi_handle_t *ffado_driver_midi_init(ffado_driver_t *driver) {
// 	int err;

	char buf[256];
	channel_t chn;
	int nchannels;
	int i=0;

	ffado_device_t *dev=driver->dev;

	assert(dev);

	ffado_driver_midi_handle_t *m=calloc(1,sizeof(ffado_driver_midi_handle_t));
	if (!m) {
		printError("not enough memory to create midi structure");
		return NULL;
	}

	if (snd_seq_open(&m->seq_handle, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) {
		printError("Error opening ALSA sequencer.");
		free(m);
		return NULL;
	}

	snd_seq_set_client_name(m->seq_handle, "FreeBoB Jack MIDI");

	// find out the number of midi in/out ports we need to setup
	nchannels=ffado_streaming_get_nb_capture_streams(dev);

	m->nb_input_ports=0;

	for (chn = 0; chn < nchannels; chn++) {	
		if(ffado_streaming_get_capture_stream_type(dev, chn) == ffado_stream_type_midi) {
			m->nb_input_ports++;
		}
	}

	m->input_ports=calloc(m->nb_input_ports,sizeof(ffado_midi_port_t *));
	if(!m->input_ports) {
		printError("not enough memory to create midi structure");
		free(m);
		return NULL;
	}

	i=0;
	for (chn = 0; chn < nchannels; chn++) {
		if(ffado_streaming_get_capture_stream_type(dev, chn) == ffado_stream_type_midi) {
			m->input_ports[i]=calloc(1,sizeof(ffado_midi_port_t));
			if(!m->input_ports[i]) {
				// fixme
				printError("Could not allocate memory for seq port");
				continue;
			}

	 		ffado_streaming_get_capture_stream_name(dev, chn, buf, sizeof(buf) - 1);
			printMessage("Register MIDI IN port %s", buf);

			m->input_ports[i]->seq_port_nr=snd_seq_create_simple_port(m->seq_handle, buf,
				SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ,
				SND_SEQ_PORT_TYPE_MIDI_GENERIC);

			if(m->input_ports[i]->seq_port_nr<0) {
				printError("Could not create seq port");
				m->input_ports[i]->stream_nr=-1;
				m->input_ports[i]->seq_port_nr=-1;
			} else {
				m->input_ports[i]->stream_nr=chn;
				m->input_ports[i]->seq_handle=m->seq_handle;
				if (snd_midi_event_new  ( ALSA_SEQ_BUFF_SIZE, &(m->input_ports[i]->parser)) < 0) {
					printError("could not init parser for MIDI IN port %d",i);
					m->input_ports[i]->stream_nr=-1;
					m->input_ports[i]->seq_port_nr=-1;
				} else {
					if(ffado_streaming_set_capture_buffer_type(dev, chn, ffado_buffer_type_midi)) {
						printError(" cannot set port buffer type for %s", buf);
						m->input_ports[i]->stream_nr=-1;
						m->input_ports[i]->seq_port_nr=-1;
					}
					if(ffado_streaming_capture_stream_onoff(dev, chn, 1)) {
						printError(" cannot enable port %s", buf);
						m->input_ports[i]->stream_nr=-1;
						m->input_ports[i]->seq_port_nr=-1;
					}

				}
			}

			i++;
		}
	}

	// playback
	nchannels=ffado_streaming_get_nb_playback_streams(dev);

	m->nb_output_ports=0;

	for (chn = 0; chn < nchannels; chn++) {	
		if(ffado_streaming_get_playback_stream_type(dev, chn) == ffado_stream_type_midi) {
			m->nb_output_ports++;
		}
	}

	m->output_ports=calloc(m->nb_output_ports,sizeof(ffado_midi_port_t *));
	if(!m->output_ports) {
		printError("not enough memory to create midi structure");
		for (i = 0; i < m->nb_input_ports; i++) {	
			free(m->input_ports[i]);
		}
		free(m->input_ports);
		free(m);
		return NULL;
	}

	i=0;
	for (chn = 0; chn < nchannels; chn++) {
		if(ffado_streaming_get_playback_stream_type(dev, chn) == ffado_stream_type_midi) {
			m->output_ports[i]=calloc(1,sizeof(ffado_midi_port_t));
			if(!m->output_ports[i]) {
				// fixme
				printError("Could not allocate memory for seq port");
				continue;
			}

	 		ffado_streaming_get_playback_stream_name(dev, chn, buf, sizeof(buf) - 1);
			printMessage("Register MIDI OUT port %s", buf);

			m->output_ports[i]->seq_port_nr=snd_seq_create_simple_port(m->seq_handle, buf,
				SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
              			SND_SEQ_PORT_TYPE_MIDI_GENERIC);


			if(m->output_ports[i]->seq_port_nr<0) {
				printError("Could not create seq port");
				m->output_ports[i]->stream_nr=-1;
				m->output_ports[i]->seq_port_nr=-1;
			} else {
				m->output_ports[i]->stream_nr=chn;
				m->output_ports[i]->seq_handle=m->seq_handle;
				if (snd_midi_event_new  ( ALSA_SEQ_BUFF_SIZE, &(m->output_ports[i]->parser)) < 0) {
					printError("could not init parser for MIDI OUT port %d",i);
					m->output_ports[i]->stream_nr=-1;
					m->output_ports[i]->seq_port_nr=-1;
				} else {
					if(ffado_streaming_set_playback_buffer_type(dev, chn, ffado_buffer_type_midi)) {
						printError(" cannot set port buffer type for %s", buf);
						m->input_ports[i]->stream_nr=-1;
						m->input_ports[i]->seq_port_nr=-1;
					}
					if(ffado_streaming_playback_stream_onoff(dev, chn, 1)) {
						printError(" cannot enable port %s", buf);
						m->input_ports[i]->stream_nr=-1;
						m->input_ports[i]->seq_port_nr=-1;
					}
				}
			}

			i++;
		}
	}

	m->dev=dev;
	m->driver=driver;

	return m;
}

static int
ffado_driver_midi_start (ffado_driver_midi_handle_t *m)
{
	assert(m);
	// start threads

	m->queue_thread_realtime=(m->driver->engine->control->real_time? 1 : 0);
 	m->queue_thread_priority=
		m->driver->engine->control->client_priority +
		FFADO_RT_PRIORITY_MIDI_RELATIVE;

	if (m->queue_thread_priority>98) {
		m->queue_thread_priority=98;
	}
	if (m->queue_thread_realtime) {
		printMessage("MIDI threads running with Realtime scheduling, priority %d",
		           m->queue_thread_priority);
	} else {
		printMessage("MIDI threads running without Realtime scheduling");
	}

	if (jack_client_create_thread(NULL, &m->queue_thread, m->queue_thread_priority, m->queue_thread_realtime, ffado_driver_midi_queue_thread, (void *)m)) {
		printError(" cannot create midi queueing thread");
		return -1;
	}

	if (jack_client_create_thread(NULL, &m->dequeue_thread, m->queue_thread_priority, m->queue_thread_realtime, ffado_driver_midi_dequeue_thread, (void *)m)) {
		printError(" cannot create midi dequeueing thread");
		return -1;
	}
	return 0;
}

static int
ffado_driver_midi_stop (ffado_driver_midi_handle_t *m)
{
	assert(m);

	pthread_cancel (m->queue_thread);
	pthread_join (m->queue_thread, NULL);

	pthread_cancel (m->dequeue_thread);
	pthread_join (m->dequeue_thread, NULL);
	return 0;

}

static void
ffado_driver_midi_finish (ffado_driver_midi_handle_t *m)
{
	assert(m);

	int i;
	// TODO: add state info here, if not stopped then stop

	for (i=0;i<m->nb_input_ports;i++) {
		free(m->input_ports[i]);

	}
	free(m->input_ports);

	for (i=0;i<m->nb_output_ports;i++) {
		free(m->output_ports[i]);
	}
	free(m->output_ports);

	free(m);
}
#endif	
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
	desc->nparams = 10;
  
	params = calloc (desc->nparams, sizeof (jack_driver_param_desc_t));
	desc->params = params;

	i = 0;
	strcpy (params[i].name, "device");
	params[i].character  = 'd';
	params[i].type       = JackDriverParamString;
	strcpy (params[i].value.str,  "hw:0");
	strcpy (params[i].short_desc, "The FireWire device to use. Format is: 'hw:port[,node]'.");
	strcpy (params[i].long_desc,  params[i].short_desc);
	
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

	return desc;
}


jack_driver_t *
driver_initialize (jack_client_t *client, JSList * params)
{
	jack_driver_t *driver;

    unsigned int port=0;
    unsigned int node_id=-1;
    int nbitems;
      
	const JSList * node;
	const jack_driver_param_t * param;

	ffado_jack_settings_t cmlparams;
	
    char *device_name="hw:0"; 
      
	cmlparams.period_size_set=0;
	cmlparams.sample_rate_set=0;
	cmlparams.buffer_size_set=0;
	cmlparams.port_set=0;
	cmlparams.node_id_set=0;

	/* default values */
	cmlparams.period_size=1024;
	cmlparams.sample_rate=48000;
	cmlparams.buffer_size=3;
	cmlparams.port=0;
	cmlparams.node_id=-1;
	cmlparams.playback_ports=1;
	cmlparams.capture_ports=1;
	cmlparams.playback_frame_latency=0;
	cmlparams.capture_frame_latency=0;
	cmlparams.slave_mode=0;
	cmlparams.snoop_mode=0;
	
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
		}
	}
	
    nbitems=sscanf(device_name,"hw:%u,%u",&port,&node_id);
    if (nbitems<2) {
        nbitems=sscanf(device_name,"hw:%u",&port);
      
        if(nbitems < 1) {
            free(device_name);
            printError("device (-d) argument not valid\n");
            return NULL;
        } else {
            cmlparams.port = port;
            cmlparams.port_set=1;
            
            cmlparams.node_id = -1;
            cmlparams.node_id_set=0;
        }
     } else {
        cmlparams.port = port;
        cmlparams.port_set=1;
        
        cmlparams.node_id = node_id;
        cmlparams.node_id_set=1;
     }

    jack_error("FFADO using Firewire port %d, node %d",cmlparams.port,cmlparams.node_id);
    
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
