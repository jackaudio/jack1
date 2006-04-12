/*
 *   FreeBob Backend for Jack
 *   FreeBob = Firewire (pro-)audio for linux
 *
 *   http://freebob.sf.net
 *   http://jackit.sf.net
 *
 *   Copyright (C) 2005 Pieter Palmers <pieterpalmers@users.sourceforge.net>
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

#include "freebob_driver.h"

#define SAMPLE_MAX_24BIT  8388608.0f
#define SAMPLE_MAX_16BIT  32768.0f

static int freebob_driver_stop (freebob_driver_t *driver);

#ifdef FREEBOB_DRIVER_WITH_MIDI
	static freebob_driver_midi_handle_t *freebob_driver_midi_init(freebob_driver_t *driver);
	static void freebob_driver_midi_finish (freebob_driver_midi_handle_t *m);
#endif

static int
freebob_driver_attach (freebob_driver_t *driver)
{
	char buf[64];
	channel_t chn;
	jack_port_t *port;
	int port_flags;

	driver->engine->set_buffer_size (driver->engine, driver->period_size);
	driver->engine->set_sample_rate (driver->engine, driver->sample_rate);

	port_flags = JackPortIsOutput|JackPortIsPhysical|JackPortIsTerminal;

	driver->capture_nchannels=freebob_streaming_get_nb_capture_streams(driver->dev);

	for (chn = 0; chn < driver->capture_nchannels; chn++) {
		
		freebob_streaming_get_capture_stream_name(driver->dev, chn, buf, sizeof(buf) - 1);
		
		if(freebob_streaming_get_capture_stream_type(driver->dev, chn) != freebob_stream_type_audio) {
			printMessage ("Don't register capture port %s\n", buf);
// 			continue;
			// we have to add a NULL entry in the list to be able to loop over the channels in the read/write routines
			driver->capture_ports =
				jack_slist_append (driver->capture_ports, NULL);
		} else {
			printMessage ("Registering capture port %s\n", buf);
			if ((port = jack_port_register (driver->client, buf,
							JACK_DEFAULT_AUDIO_TYPE,
							port_flags, 0)) == NULL) {
				jack_error ("FREEBOB: cannot register port for %s", buf);
				break;
			}
			driver->capture_ports =
				jack_slist_append (driver->capture_ports, port);
		}


// 		jack_port_set_latency (port, driver->period_size);

	}
	
	port_flags = JackPortIsInput|JackPortIsPhysical|JackPortIsTerminal;

	driver->playback_nchannels=freebob_streaming_get_nb_playback_streams(driver->dev);

	for (chn = 0; chn < driver->playback_nchannels; chn++) {

		freebob_streaming_get_playback_stream_name(driver->dev, chn, buf, sizeof(buf) - 1);
		
		if(freebob_streaming_get_playback_stream_type(driver->dev, chn) != freebob_stream_type_audio) {
			printMessage ("Don't register playback port %s\n", buf);
// 			continue;
			// we have to add a NULL entry in the list to be able to loop over the channels in the read/write routines
			driver->playback_ports =
				jack_slist_append (driver->playback_ports, NULL);
		} else {
			printMessage ("Registering playback port %s\n", buf);
			if ((port = jack_port_register (driver->client, buf,
							JACK_DEFAULT_AUDIO_TYPE,
							port_flags, 0)) == NULL) {
				jack_error ("FREEBOB: cannot register port for %s", buf);
				break;
			}
			driver->playback_ports =
				jack_slist_append (driver->playback_ports, port);
		}
// 		jack_port_set_latency (port, (driver->period_size * (driver->user_nperiods - 1)) + driver->playback_frame_latency);

	}

	return jack_activate (driver->client);
}

static int
freebob_driver_detach (freebob_driver_t *driver)
{
	JSList *node;

	if (driver->engine == NULL) {
		return 0;
	}

	for (node = driver->capture_ports; node;
	     node = jack_slist_next (node)) {
		jack_port_unregister (driver->client,
				      ((jack_port_t *) node->data));
	}

	jack_slist_free (driver->capture_ports);
	driver->capture_ports = 0;
		
	for (node = driver->playback_ports; node;
	     node = jack_slist_next (node)) {
		jack_port_unregister (driver->client,
				      ((jack_port_t *) node->data));
	}

	jack_slist_free (driver->playback_ports);
	driver->playback_ports = 0;

	return 0;

}

static inline void 
freebob_driver_read_from_channel (freebob_driver_t *driver,
			       channel_t channel,
			       jack_default_audio_sample_t *dst,
			       jack_nframes_t nsamples)
{
	
	freebob_sample_t buffer[nsamples];
	char *src=(char *)buffer;
	
	freebob_streaming_read(driver->dev, channel, buffer, nsamples);
	
#if 0
	int i=0;
	if(channel==0) {
		jack_error("Read for channel %d",channel);
		for (i=0;i<nsamples;i+=8) {
			fprintf(stderr," %08X %08X %08X %08X %08X %08X %08X %08X\n",
				buffer[i],
				buffer[i+1],
				buffer[i+2],
				buffer[i+3],
				buffer[i+4],
				buffer[i+5],
				buffer[i+6],
				buffer[i+7]
				);	
		}
	}	
#endif
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
		src += sizeof(freebob_sample_t);
	}
	
}

static int
freebob_driver_read (freebob_driver_t * driver, jack_nframes_t nframes)
{
	jack_default_audio_sample_t* buf;
	channel_t chn;
	JSList *node;
	jack_port_t* port;
	
	freebob_sample_t nullbuffer[nframes];

	freebob_streaming_stream_type stream_type;
	
	printEnter();
	
	for (chn = 0, node = driver->capture_ports; node; node = jack_slist_next (node), chn++) {
		stream_type = freebob_streaming_get_capture_stream_type(driver->dev, chn);
		if(stream_type == freebob_stream_type_audio) {
			port = (jack_port_t *) node->data;

			buf = jack_port_get_buffer (port, nframes);
			if(!buf) buf=(jack_default_audio_sample_t*)nullbuffer;
				
			freebob_streaming_set_capture_stream_buffer(driver->dev, chn, (char *)(buf), freebob_buffer_type_float);
		} else if(stream_type == freebob_stream_type_midi) {
			// these should be read/written with the per-stream functions

		} else { // empty other buffers without doing something with them
			freebob_streaming_set_capture_stream_buffer(driver->dev, chn, (char *)(nullbuffer), freebob_buffer_type_uint24);
		}
	}

	// now transfer the buffers
	freebob_streaming_transfer_capture_buffers(driver->dev);
	
	printExit();
	
	return 0;

}

static inline void 
freebob_driver_write_to_channel (freebob_driver_t *driver,
			      channel_t channel, 
			      jack_default_audio_sample_t *buf, 
			      jack_nframes_t nsamples)
{
    long long y;
	freebob_sample_t buffer[nsamples];
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
		dst += sizeof(freebob_sample_t);
		buf++;
	}
	
	// write to the freebob streaming device
	freebob_streaming_write(driver->dev, channel, buffer, nsamples);
	
}

static int
freebob_driver_write (freebob_driver_t * driver, jack_nframes_t nframes)
{
	channel_t chn;
	JSList *node;
	jack_default_audio_sample_t* buf;

	jack_port_t *port;

	freebob_streaming_stream_type stream_type;

	freebob_sample_t nullbuffer[nframes];

	memset(&nullbuffer,0,nframes*sizeof(freebob_sample_t));

	printEnter();

	driver->process_count++;

	assert(driver->dev);

 	if (driver->engine->freewheeling) {
 		return 0;
 	}

	for (chn = 0, node = driver->playback_ports; node; node = jack_slist_next (node), chn++) {
		stream_type=freebob_streaming_get_playback_stream_type(driver->dev, chn);
		if(stream_type == freebob_stream_type_audio) {
			port = (jack_port_t *) node->data;

			buf = jack_port_get_buffer (port, nframes);
			if(!buf) buf=(jack_default_audio_sample_t*)nullbuffer;
				
			freebob_streaming_set_playback_stream_buffer(driver->dev, chn, (char *)(buf), freebob_buffer_type_float);

		} else if(stream_type == freebob_stream_type_midi) {
			// these should be read/written with the per-stream functions

		} else { // empty other buffers without doing something with them
			freebob_streaming_set_playback_stream_buffer(driver->dev, chn, (char *)(nullbuffer), freebob_buffer_type_uint24);
		}
	}

	freebob_streaming_transfer_playback_buffers(driver->dev);

	printExit();
	
	return 0;
}

//static inline jack_nframes_t 
static jack_nframes_t 
freebob_driver_wait (freebob_driver_t *driver, int extra_fd, int *status,
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

	nframes=freebob_streaming_wait(driver->dev);
	
	wait_ret = jack_get_microseconds ();
	
	if (driver->wait_next && wait_ret > driver->wait_next) {
		*delayed_usecs = wait_ret - driver->wait_next;
	}
	driver->wait_last = wait_ret;
	driver->wait_next = wait_ret + driver->period_usecs;
	driver->engine->transport_cycle_start (driver->engine, wait_ret);
	
	// transfer the streaming buffers
	// we now do this in the read/write functions
// 	freebob_streaming_transfer_buffers(driver->dev);
	
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
freebob_driver_run_cycle (freebob_driver_t *driver)
{
	jack_engine_t *engine = driver->engine;
	int wait_status=0;
	float delayed_usecs=0.0;

	jack_nframes_t nframes = freebob_driver_wait (driver, -1, &wait_status,
						   &delayed_usecs);
	
	if ((wait_status < 0)) {
		jack_error( "wait status < 0! (= %d)\n",wait_status);
		return -1;
	}
		
	if ((nframes == 0)) {
		/* we detected an xrun and restarted: notify
		 * clients about the delay. */
		jack_error( "xrun detected\n");
		engine->delay (engine, delayed_usecs);
		return 0;
	} 
	
	return engine->run_cycle (engine, nframes, delayed_usecs);

}
/*
 * in a null cycle we should discard the input and write silence to the outputs
 */
static int
freebob_driver_null_cycle (freebob_driver_t* driver, jack_nframes_t nframes)
{
	jack_error("Null cycle...\n");
	channel_t chn;
	JSList *node;
	snd_pcm_sframes_t nwritten;

	freebob_streaming_stream_type stream_type;

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
		stream_type=freebob_streaming_get_playback_stream_type(driver->dev, chn);

		if(stream_type == freebob_stream_type_audio) {
			freebob_streaming_set_playback_stream_buffer(driver->dev, chn, (char *)(buffer), freebob_buffer_type_float);

		} else if(stream_type == freebob_stream_type_midi) {
			// these should be read/written with the per-stream functions

		} else { // empty other buffers without doing something with them
			freebob_streaming_set_playback_stream_buffer(driver->dev, chn, (char *)(buffer), freebob_buffer_type_uint24);
		}
	}

	freebob_streaming_transfer_playback_buffers(driver->dev);
	
	// read & discard from input ports
	for (chn = 0, node = driver->capture_ports; node; node = jack_slist_next (node), chn++) {
		stream_type=freebob_streaming_get_capture_stream_type(driver->dev, chn);
		if(stream_type == freebob_stream_type_audio) {
			freebob_streaming_set_capture_stream_buffer(driver->dev, chn, (char *)(buffer), freebob_buffer_type_float);

		} else if(stream_type == freebob_stream_type_midi) {

		} else { // empty other buffers without doing something with them
		}
	}

	// now transfer the buffers
	freebob_streaming_transfer_capture_buffers(driver->dev);
		
	printExit();
	return 0;
}

static int
freebob_driver_start (freebob_driver_t *driver)
{
	jack_error("Driver start...\n");
	return freebob_streaming_start(driver->dev);

}

static int
freebob_driver_stop (freebob_driver_t *driver)
{
	jack_error("Driver stop...\n");
	
	return freebob_streaming_stop(driver->dev);
}


static int
freebob_driver_bufsize (freebob_driver_t* driver, jack_nframes_t nframes)
{
	jack_error("Buffer size change requested!!!\n");

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

freebob_driver_t *
freebob_driver_new (jack_client_t * client,
		  char *name,
		  freebob_jack_settings_t *params)
{
	freebob_driver_t *driver;
	freebob_device_info_t device_info;
	freebob_options_t device_options;

	assert(params);

	jack_error("Starting Freebob backend (%s)", freebob_get_version());

	driver = calloc (1, sizeof (freebob_driver_t));

	/* Setup the jack interfaces */  
	jack_driver_nt_init ((jack_driver_nt_t *) driver);

	driver->nt_attach    = (JackDriverNTAttachFunction)   freebob_driver_attach;
	driver->nt_detach    = (JackDriverNTDetachFunction)   freebob_driver_detach;
	driver->nt_start     = (JackDriverNTStartFunction)    freebob_driver_start;
	driver->nt_stop      = (JackDriverNTStopFunction)     freebob_driver_stop;
	driver->nt_run_cycle = (JackDriverNTRunCycleFunction) freebob_driver_run_cycle;
	driver->null_cycle   = (JackDriverNullCycleFunction)  freebob_driver_null_cycle;
	driver->write        = (JackDriverReadFunction)       freebob_driver_write;
	driver->read         = (JackDriverReadFunction)       freebob_driver_read;
	driver->nt_bufsize   = (JackDriverNTBufSizeFunction)  freebob_driver_bufsize;
	
	/* copy command line parameter contents to the driver structure */
	memcpy(&driver->settings,params,sizeof(freebob_jack_settings_t));
	
	/* prepare all parameters */
	driver->sample_rate = params->sample_rate;
	driver->period_size = params->period_size;
	driver->last_wait_ust = 0;
	
	driver->period_usecs =
		(jack_time_t) floor ((((float) driver->period_size) * 1000000.0f) / driver->sample_rate);

	driver->client = client;
	driver->engine = NULL;

	memset(&device_options,0,sizeof(device_options));	
	device_options.sample_rate=params->sample_rate;
	device_options.period_size=params->period_size;
	device_options.nb_buffers=params->buffer_size;
	device_options.iso_buffers=params->iso_buffers;
	device_options.iso_prebuffers=params->iso_prebuffers;
	device_options.iso_irq_interval=params->iso_irq_interval;
	device_options.node_id=params->node_id;
	device_options.port=params->port;

	/* packetizer thread options */

	device_options.realtime=FREEBOB_USE_RT;
	device_options.packetizer_priority=FREEBOB_RT_PRIORITY_PACKETIZER;
	
	driver->dev=freebob_streaming_init(&device_info,device_options);

	if(!driver->dev) {
		jack_error("FREEBOB: Error creating virtual device");
		jack_driver_nt_finish ((jack_driver_nt_t *) driver);
		free (driver);
		return NULL;
	}

#ifdef FREEBOB_DRIVER_WITH_MIDI
	driver->midi_handle=freebob_driver_midi_init(driver);
	if(!driver->midi_handle) {
		jack_error("FREEBOB: Error creating midi device");
		jack_driver_nt_finish ((jack_driver_nt_t *) driver);
		free (driver);
		return NULL;
	}
#endif

	jack_error("FREEBOB: Driver compiled on %s %s", __DATE__, __TIME__);
	jack_error("FREEBOB: Created driver %s", name);
	jack_error("            period_size: %d", driver->period_size);
	jack_error("            period_usecs: %d", driver->period_usecs);
	jack_error("            sample rate: %d", driver->sample_rate);
	if (device_options.realtime) {
		jack_error("            running with Realtime scheduling, priority %d", device_options.packetizer_priority);
	} else {
		jack_error("            running without Realtime scheduling");
	}



	return (freebob_driver_t *) driver;

}

static void
freebob_driver_delete (freebob_driver_t *driver)
{

	freebob_streaming_finish(driver->dev);

#ifdef FREEBOB_DRIVER_WITH_MIDI
	freebob_driver_midi_finish(driver->midi_handle);	
#endif	

	jack_driver_nt_finish ((jack_driver_nt_t *) driver);
	free (driver);
}

#ifdef FREEBOB_DRIVER_WITH_MIDI
/*
 * MIDI support
 */ 

// the thread that will queue the midi events from the seq to the stream buffers

void * freebob_driver_midi_queue_thread(void *arg)
{
	freebob_driver_midi_handle_t *m=(freebob_driver_midi_handle_t *)arg;
	assert(m);
	snd_seq_event_t *ev;
	unsigned char work_buffer[MIDI_TRANSMIT_BUFFER_SIZE];
	int bytes_to_send;
	int b;
	int i;

	jack_error ("FREEBOB: MIDI queue thread started");

	while(1) {
		// get next event, if one is present
		while ((snd_seq_event_input(m->seq_handle, &ev) > 0)) {
			// get the port this event is originated from
			freebob_midi_port_t *port=NULL;
			for (i=0;i<m->nb_output_ports;i++) {
				if(m->output_ports[i]->seq_port_nr == ev->dest.port) {
					port=m->output_ports[i];
					break;
				}
			}
	
			if(!port) {
				jack_error ("FREEBOB: Could not find target port for event: dst=%d src=%d\n", ev->dest.port, ev->source.port);

				break;
			}
			
			// decode it to the work buffer
			if((bytes_to_send = snd_midi_event_decode ( port->parser, 
				work_buffer,
				MIDI_TRANSMIT_BUFFER_SIZE, 
				ev))<0) 
			{ // failed
				jack_error ("FREEBOB: Error decoding event for port %d (errcode=%d)\n", port->seq_port_nr,bytes_to_send);
				bytes_to_send=0;
				//return -1;
			}
	
			for(b=0;b<bytes_to_send;b++) {
				freebob_sample_t tmp_event=work_buffer[b];
				if(freebob_streaming_write(m->dev, port->stream_nr, &tmp_event, 1)<1) {
					jack_error ("FREEBOB: Midi send buffer overrun\n");
				}
			}
	
		}

		// sleep for some time
		usleep(MIDI_THREAD_SLEEP_TIME_USECS);
	}
	return NULL;
}

// the dequeue thread (maybe we need one thread per stream)
void *freebob_driver_midi_dequeue_thread (void *arg) {
	freebob_driver_midi_handle_t *m=(freebob_driver_midi_handle_t *)arg;

	int i;
	int s;
	
	int samples_read;

	assert(m);

	while(1) {
		// read incoming events
	
		for (i=0;i<m->nb_input_ports;i++) {
			unsigned int buff[64];
	
			freebob_midi_port_t *port=m->input_ports[i];
		
			if(!port) {
				jack_error("FREEBOB: something went wrong when setting up the midi input port map (%d)",i);
			}
		
			do {
				samples_read=freebob_streaming_read(m->dev, port->stream_nr, buff, 64);
			
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

static freebob_driver_midi_handle_t *freebob_driver_midi_init(freebob_driver_t *driver) {
// 	int err;

	char buf[256];
	channel_t chn;
	int nchannels;
	int i=0;

	freebob_device_t *dev=driver->dev;

	assert(dev);

	freebob_driver_midi_handle_t *m=calloc(1,sizeof(freebob_driver_midi_handle_t));
	if (!m) {
		jack_error("FREEBOB: not enough memory to create midi structure");
		return NULL;
	}

	if (snd_seq_open(&m->seq_handle, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) {
		jack_error("FREEBOB: Error opening ALSA sequencer.");
		free(m);
		return NULL;
	}

	snd_seq_set_client_name(m->seq_handle, "FreeBoB Jack MIDI");

	// find out the number of midi in/out ports we need to setup
	nchannels=freebob_streaming_get_nb_capture_streams(dev);

	m->nb_input_ports=0;

	for (chn = 0; chn < nchannels; chn++) {	
		if(freebob_streaming_get_capture_stream_type(dev, chn) == freebob_stream_type_midi) {
			m->nb_input_ports++;
		}
	}

	m->input_ports=calloc(m->nb_input_ports,sizeof(freebob_midi_port_t *));
	if(!m->input_ports) {
		jack_error("FREEBOB: not enough memory to create midi structure");
		free(m);
		return NULL;
	}

	i=0;
	for (chn = 0; chn < nchannels; chn++) {
		if(freebob_streaming_get_capture_stream_type(dev, chn) == freebob_stream_type_midi) {
			m->input_ports[i]=calloc(1,sizeof(freebob_midi_port_t));
			if(!m->input_ports[i]) {
				// fixme
				jack_error("FREEBOB: Could not allocate memory for seq port");
				continue;
			}

	 		freebob_streaming_get_capture_stream_name(dev, chn, buf, sizeof(buf) - 1);
			jack_error("FREEBOB: Register MIDI IN port %s\n", buf);

			m->input_ports[i]->seq_port_nr=snd_seq_create_simple_port(m->seq_handle, buf,
				SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_SUBS_READ,
				SND_SEQ_PORT_TYPE_MIDI_GENERIC);

			if(m->input_ports[i]->seq_port_nr<0) {
				jack_error("FREEBOB: Could not create seq port");
				m->input_ports[i]->stream_nr=-1;
				m->input_ports[i]->seq_port_nr=-1;
			} else {
				m->input_ports[i]->stream_nr=chn;
				m->input_ports[i]->seq_handle=m->seq_handle;
				if (snd_midi_event_new  ( ALSA_SEQ_BUFF_SIZE, &(m->input_ports[i]->parser)) < 0) {
					fprintf(stderr, "FREEBOB: could not init parser for MIDI IN port %d\n",i);
					m->input_ports[i]->stream_nr=-1;
					m->input_ports[i]->seq_port_nr=-1;
				}
			}

			i++;
		}
	}

	// playback
	nchannels=freebob_streaming_get_nb_playback_streams(dev);

	m->nb_output_ports=0;

	for (chn = 0; chn < nchannels; chn++) {	
		if(freebob_streaming_get_playback_stream_type(dev, chn) == freebob_stream_type_midi) {
			m->nb_output_ports++;
		}
	}

	m->output_ports=calloc(m->nb_output_ports,sizeof(freebob_midi_port_t *));
	if(!m->output_ports) {
		jack_error("FREEBOB: not enough memory to create midi structure");
		for (i = 0; i < m->nb_input_ports; i++) {	
			free(m->input_ports[i]);
		}
		free(m->input_ports);
		free(m);
		return NULL;
	}

	i=0;
	for (chn = 0; chn < nchannels; chn++) {
		if(freebob_streaming_get_playback_stream_type(dev, chn) == freebob_stream_type_midi) {
			m->output_ports[i]=calloc(1,sizeof(freebob_midi_port_t));
			if(!m->output_ports[i]) {
				// fixme
				jack_error("FREEBOB: Could not allocate memory for seq port");
				continue;
			}

	 		freebob_streaming_get_playback_stream_name(dev, chn, buf, sizeof(buf) - 1);
			jack_error("FREEBOB: Register MIDI OUT port %s\n", buf);

			m->output_ports[i]->seq_port_nr=snd_seq_create_simple_port(m->seq_handle, buf,
				SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
              			SND_SEQ_PORT_TYPE_MIDI_GENERIC);


			if(m->output_ports[i]->seq_port_nr<0) {
				jack_error("FREEBOB: Could not create seq port");
				m->output_ports[i]->stream_nr=-1;
				m->output_ports[i]->seq_port_nr=-1;
			} else {
				m->output_ports[i]->stream_nr=chn;
				m->output_ports[i]->seq_handle=m->seq_handle;
				if (snd_midi_event_new  ( ALSA_SEQ_BUFF_SIZE, &(m->output_ports[i]->parser)) < 0) {
					fprintf(stderr, "FREEBOB: could not init parser for MIDI OUT port %d\n",i);
					m->output_ports[i]->stream_nr=-1;
					m->output_ports[i]->seq_port_nr=-1;
				}
			}

			i++;
		}
	}


	// start threads
	m->dev=dev;

 	m->queue_thread_priority=FREEBOB_RT_PRIORITY_MIDI;
	m->queue_thread_realtime=FREEBOB_USE_RT;

	if (jack_client_create_thread(NULL, &m->queue_thread, m->queue_thread_priority, m->queue_thread_realtime, freebob_driver_midi_queue_thread, (void *)m)) {
		jack_error("FREEBOB: cannot create midi queueing thread");
		free(m);
		return NULL;
	}

	if (jack_client_create_thread(NULL, &m->dequeue_thread, m->queue_thread_priority, m->queue_thread_realtime, freebob_driver_midi_dequeue_thread, (void *)m)) {
		jack_error("FREEBOB: cannot create midi dequeueing thread");
		free(m);
		return NULL;
	}
	return m;
}

static void
freebob_driver_midi_finish (freebob_driver_midi_handle_t *m)
{
	assert(m);

	int i;

	pthread_cancel (m->queue_thread);
	pthread_join (m->queue_thread, NULL);

	pthread_cancel (m->dequeue_thread);
	pthread_join (m->dequeue_thread, NULL);


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

const char driver_client_name[] = "freebob_pcm";

const jack_driver_desc_t *
driver_get_descriptor ()
{
	jack_driver_desc_t * desc;
	jack_driver_param_desc_t * params;
	unsigned int i;

	desc = calloc (1, sizeof (jack_driver_desc_t));

	strcpy (desc->name, "freebob");
	desc->nparams = 8;
  
	params = calloc (desc->nparams, sizeof (jack_driver_param_desc_t));
	desc->params = params;

	i = 0;
	strcpy (params[i].name, "port");
	params[i].character  = 'd';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 0;
	strcpy (params[i].short_desc, "The FireWire port to use");
	strcpy (params[i].long_desc, params[i].short_desc);
	
	i++;
	strcpy (params[i].name, "node");
	params[i].character  = 'n';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = -1;
	strcpy (params[i].short_desc, "Node id of the BeBoB device");
	strcpy (params[i].long_desc, "The node id of the BeBoB device on the FireWire bus\n"
                                      "(use -1 to use scan all devices on the bus)");
	i++;
	strcpy (params[i].name, "period-size");
	params[i].character  = 'p';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 512;
	strcpy (params[i].short_desc, "Period size");
	strcpy (params[i].long_desc, params[i].short_desc);
	
	i++;
	strcpy (params[i].name, "nb-buffers");
	params[i].character  = 'r';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 3;
	strcpy (params[i].short_desc, "Number of periods to buffer");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "buffer-size");
	params[i].character  = 'b';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 100U;
	strcpy (params[i].short_desc, "The RAW1394 buffer size to use (in frames)");
	strcpy (params[i].long_desc, params[i].short_desc);
	
	i++;
	strcpy (params[i].name, "prebuffer-size");
	params[i].character  = 's';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 0U;
	strcpy (params[i].short_desc, "The RAW1394 pre-buffer size to use (in frames)");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "irq-interval");
	params[i].character  = 'i';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 4U;
	strcpy (params[i].short_desc, "The interrupt interval to use (in packets)");
	strcpy (params[i].long_desc, params[i].short_desc);

	i++;
	strcpy (params[i].name, "samplerate");
	params[i].character  = 'a';
	params[i].type       = JackDriverParamUInt;
	params[i].value.ui   = 44100U;
	strcpy (params[i].short_desc, "The sample rate");
	strcpy (params[i].long_desc, params[i].short_desc);
	
	return desc;
}


jack_driver_t *
driver_initialize (jack_client_t *client, JSList * params)
{
	jack_driver_t *driver;

	const JSList * node;
	const jack_driver_param_t * param;
			
	freebob_jack_settings_t cmlparams;
	
       
	cmlparams.period_size_set=0;
	cmlparams.sample_rate_set=0;
	cmlparams.fifo_size_set=0;
	cmlparams.table_size_set=0;
	cmlparams.iso_buffers_set=0;
	cmlparams.iso_prebuffers_set=0;
	cmlparams.iso_irq_interval_set=0;
	cmlparams.buffer_size_set=0;
	cmlparams.port_set=0;
	cmlparams.node_id_set=0;

	/* default values */
	cmlparams.period_size=512;
	cmlparams.sample_rate=44100;
	cmlparams.iso_buffers=100;
	cmlparams.iso_prebuffers=0;
	cmlparams.iso_irq_interval=4;
	cmlparams.buffer_size=3;
	cmlparams.port=0;
	cmlparams.node_id=-1;
	
	for (node = params; node; node = jack_slist_next (node))
	{
		param = (jack_driver_param_t *) node->data;

		switch (param->character)
		{
		case 'd':
			cmlparams.port = param->value.ui;
			cmlparams.port_set=1;
			break;
		case 'n':
			cmlparams.node_id = param->value.ui;
			cmlparams.node_id_set=1;
			break;
		case 'p':
			cmlparams.period_size = param->value.ui;
			cmlparams.period_size_set = 1;
			break;
		case 'b':
			cmlparams.iso_buffers = param->value.ui;
			cmlparams.iso_buffers_set = 1;
			break;
		case 'r':
			cmlparams.buffer_size = param->value.ui;
			cmlparams.buffer_size_set = 1;
			break;        
		case 's':
			cmlparams.iso_prebuffers = param->value.ui;
			cmlparams.iso_prebuffers_set = 1;
			break;
		case 'i':
			cmlparams.iso_irq_interval = param->value.ui;
			cmlparams.iso_irq_interval_set = 1;
			break;
		case 'a':
			cmlparams.sample_rate = param->value.ui;
			cmlparams.sample_rate_set = 1;
			break;
		}
	}
	
	driver=(jack_driver_t *)freebob_driver_new (client, "freebob_pcm", &cmlparams);

	return driver;
}

void
driver_finish (jack_driver_t *driver)
{
	freebob_driver_t *drv=(freebob_driver_t *) driver;
	
	freebob_driver_delete (drv);

}
