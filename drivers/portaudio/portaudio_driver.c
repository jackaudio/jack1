/* -*- mode: c; c-file-style: "bsd"; -*- */
/*
    Copyright © Grame 2003

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

    Grame Research Laboratory, 9, rue du Garet 69001 Lyon - France
    grame@rd.grame.fr
    
*/

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <jack/engine.h>
#include "portaudio_driver.h"


static int
paCallback(void *inputBuffer, void *outputBuffer,
                             unsigned long framesPerBuffer,
                             PaTimestamp outTime, void *userData)
{
	portaudio_driver_t * driver = (portaudio_driver_t*)userData;
    
        driver->inPortAudio = (float*)inputBuffer;
	driver->outPortAudio = (float*)outputBuffer;
        
        return driver->engine->run_cycle(driver->engine, framesPerBuffer, 0);
}

static int
portaudio_driver_attach (portaudio_driver_t *driver, jack_engine_t *engine)
{
        jack_port_t *port;
        int port_flags;
        channel_t chn;
        char buf[32];
            
        driver->engine = engine;
        
        driver->engine->set_buffer_size (engine, driver->frames_per_cycle);
        driver->engine->set_sample_rate (engine, driver->frame_rate);
        
        port_flags = JackPortIsOutput|JackPortIsPhysical|JackPortIsTerminal;
    
        /*
        if (driver->has_hw_monitoring) {
                port_flags |= JackPortCanMonitor;
        }
        */
    
        for (chn = 0; chn < driver->capture_nchannels; chn++) {
    
                snprintf (buf, sizeof(buf) - 1, "capture_%lu", chn+1);
    
                if ((port = jack_port_register (driver->client, buf, JACK_DEFAULT_AUDIO_TYPE, port_flags, 0)) == NULL) {
                        jack_error ("portaudio: cannot register port for %s", buf);
                        break;
                }
    
                /* XXX fix this so that it can handle: systemic (external) latency
                */
    
                jack_port_set_latency (port, driver->frames_per_cycle);
    
                driver->capture_ports = jack_slist_append (driver->capture_ports, port);
        }
        
        port_flags = JackPortIsInput|JackPortIsPhysical|JackPortIsTerminal;
    
        for (chn = 0; chn < driver->playback_nchannels; chn++) {
                snprintf (buf, sizeof(buf) - 1, "playback_%lu", chn+1);
    
                if ((port = jack_port_register (driver->client, buf, JACK_DEFAULT_AUDIO_TYPE, port_flags, 0)) == NULL) {
                        jack_error ("portaudio: cannot register port for %s", buf);
                        break;
                }
                
                /* XXX fix this so that it can handle: systemic (external) latency
                */
        
                jack_port_set_latency (port, driver->frames_per_cycle);
                driver->playback_ports = jack_slist_append (driver->playback_ports, port);
        }
    
        jack_activate (driver->client);
        
        return 0; 
}

static int
portaudio_driver_detach (portaudio_driver_t *driver, jack_engine_t *engine)
{
        JSList *node;
    
        if (driver->engine == 0) {
                return -1;
        }
    
        for (node = driver->capture_ports; node; node = jack_slist_next (node)) {
                jack_port_unregister (driver->client, ((jack_port_t *) node->data));
        }
    
        jack_slist_free (driver->capture_ports);
        driver->capture_ports = 0;
                
        for (node = driver->playback_ports; node; node = jack_slist_next (node)) {
                jack_port_unregister (driver->client, ((jack_port_t *) node->data));
        }
    
        jack_slist_free (driver->playback_ports);
        driver->playback_ports = 0;
        
        driver->engine = 0;
        
        return 0; 
}

static int
portaudio_driver_null_cycle (portaudio_driver_t* driver, jack_nframes_t nframes)
{
        memset(driver->outPortAudio, 0, (driver->playback_nchannels * nframes * sizeof(float)));
        return 0;
}

static int
portaudio_driver_read (portaudio_driver_t *driver, jack_nframes_t nframes)
{
        jack_default_audio_sample_t *buf;
        channel_t chn;
        jack_port_t *port;
        JSList *node;
        int i;

        for (chn = 0, node = driver->capture_ports; node; node = jack_slist_next (node), chn++) {
                
                port = (jack_port_t *)node->data;
                
                if (jack_port_connected (port) && (driver->inPortAudio != NULL)) {
                    int channels = driver->capture_nchannels;
                    float* in = driver->inPortAudio;
                    buf = jack_port_get_buffer (port, nframes); 
                    for (i = 0; i< nframes; i++) buf[i] = in[channels*i+chn];
                }
    
        }
       
        driver->engine->transport_cycle_start (driver->engine,
					       jack_get_microseconds ());
        return 0;
}          


static int
portaudio_driver_write (portaudio_driver_t *driver, jack_nframes_t nframes)
{
        jack_default_audio_sample_t *buf;
        channel_t chn;
        jack_port_t *port;
        JSList *node;
        int i;
        
        /* Clear in case of nothing is connected */
        memset(driver->outPortAudio, 0, (driver->playback_nchannels * nframes * sizeof(float)));
                
        for (chn = 0, node = driver->playback_ports; node; node = jack_slist_next (node), chn++) {
                
                port = (jack_port_t *)node->data;
                
                if (jack_port_connected (port) && (driver->outPortAudio != NULL)) {
                        int channels = driver->playback_nchannels;
                        float* out = driver->outPortAudio;
                        buf = jack_port_get_buffer (port, nframes);
                        for (i = 0; i< nframes; i++) out[channels*i+chn] = buf[i];
                }
        }
        
        return 0;
}


static int
portaudio_driver_audio_start (portaudio_driver_t *driver)
{
        PaError err = Pa_StartStream(driver->stream);
        return (err != paNoError) ? -1 : 0;
}

static int
portaudio_driver_audio_stop (portaudio_driver_t *driver)
{
        PaError err = Pa_StopStream(driver->stream);
        return (err != paNoError) ? -1 : 0;
}

static int
portaudio_driver_set_parameters (portaudio_driver_t* driver,
				   jack_nframes_t nframes,
				   jack_nframes_t rate)
{
	int capturing = driver->capturing;
	int playing = driver->playing;

	int err = Pa_OpenStream(
		&driver->stream,
		((capturing) ? Pa_GetDefaultInputDeviceID() : paNoDevice),	
		((capturing) ? driver->capture_nchannels : 0),             
		paFloat32,		/* 32-bit float input */
		NULL,
		((playing) ? Pa_GetDefaultOutputDeviceID() : paNoDevice),
		((playing) ?  driver->playback_nchannels : 0),        
		paFloat32,		/* 32-bit float output */
		NULL,
		rate,			/* sample rate */
		nframes,		/* frames per buffer */
		0,			/* number of buffers = default min */
		paClipOff,		/* we won't output out of
					 * range samples so don't
					 * bother clipping them */
		paCallback,
		driver);
    
        if (err == paNoError) {
        
		driver->period_usecs = (((float) driver->frames_per_cycle)
					/ driver->frame_rate) * 1000000.0f;
		driver->frame_rate = rate;
		driver->frames_per_cycle = nframes;

		/* tell engine about buffer size */
		if (driver->engine) {
			driver->engine->set_buffer_size (
				driver->engine, driver->frames_per_cycle);
		}
		return 0;

	} else { 

		// JOQ: this driver is dead.  How do we terminate it?
		// Pa_Terminate();
		fprintf(stderr, "Unable to set portaudio parameters\n"); 
		fprintf(stderr, "Error number: %d\n", err);
		fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
		return EIO;
	}
}

static int
portaudio_driver_reset_parameters (portaudio_driver_t* driver,
				   jack_nframes_t nframes,
				   jack_nframes_t rate)
{
	if (!jack_power_of_two(nframes)) {
		fprintf (stderr, "PA: frames must be a power of two "
			 "(64, 512, 1024, ...)\n");
		return EINVAL;
	}

        Pa_CloseStream(driver->stream);

	return portaudio_driver_set_parameters (driver, nframes, rate);
}

static int
portaudio_driver_bufsize (portaudio_driver_t* driver, jack_nframes_t nframes)
{
	int rc;

	/* This gets called from the engine server thread, so it must
	 * be serialized with the driver thread.  Stopping the audio
	 * also stops that thread. */

	if (portaudio_driver_audio_stop (driver)) {
		jack_error ("PA: cannot stop to set buffer size");
		return EIO;
	}

	rc = portaudio_driver_reset_parameters (driver, nframes,
						driver->frame_rate);

	if (portaudio_driver_audio_start (driver)) {
		jack_error ("PA: cannot restart after setting buffer size");
		rc = EIO;
	}

	return rc;
}

//== instance creation/destruction =============================================

/** create a new driver instance
 */
static jack_driver_t *
portaudio_driver_new (char *name, 
                 jack_client_t* client,
		 jack_nframes_t frames_per_cycle,
		 jack_nframes_t rate,
		 int capturing,
		 int playing,
                 int chan,
		 DitherAlgorithm dither)
{
	portaudio_driver_t *driver;
        PaError	 err = paNoError;
        const    PaDeviceInfo *pdi;
        int      numDevices;
        int      i,j;
        
	printf ("creating portaudio driver ... %" PRIu32 "|%"
		PRIu32 "\n", frames_per_cycle, rate);
    
	driver = (portaudio_driver_t *) calloc (1, sizeof (portaudio_driver_t));

	jack_driver_init ((jack_driver_t *) driver);

	if (!jack_power_of_two(frames_per_cycle)) {
		fprintf (stderr, "PA: -p must be a power of two.\n");
		goto error;
	}

	driver->frames_per_cycle = frames_per_cycle;
        driver->frame_rate = rate;
	driver->capturing = capturing;
	driver->playing = playing;

	driver->attach = (JackDriverAttachFunction) portaudio_driver_attach;
	driver->detach = (JackDriverDetachFunction) portaudio_driver_detach;
        driver->read = (JackDriverReadFunction) portaudio_driver_read;
	driver->write = (JackDriverReadFunction) portaudio_driver_write;
	driver->null_cycle = (JackDriverNullCycleFunction) portaudio_driver_null_cycle;
	driver->bufsize = (JackDriverBufSizeFunction) portaudio_driver_bufsize;
        driver->start = (JackDriverStartFunction) portaudio_driver_audio_start;
	driver->stop = (JackDriverStopFunction) portaudio_driver_audio_stop;
       
        err = Pa_Initialize();
        numDevices = Pa_CountDevices();
        
        if( numDevices < 0 )
        {
            printf("ERROR: Pa_CountDevices returned 0x%x\n", numDevices );
            err = numDevices;
            goto error;
        }
        
        printf("Number of devices = %d\n", numDevices );
        
        for( i=0; i<numDevices; i++ )
        {
            pdi = Pa_GetDeviceInfo( i );
            printf("---------------------------------------------- #%d", i );
            if( i == Pa_GetDefaultInputDeviceID() ) {
                driver->capture_nchannels = (capturing) ? pdi->maxInputChannels : 0;
            }
            if( i == Pa_GetDefaultOutputDeviceID() ){
                driver->playback_nchannels = (playing) ? pdi->maxOutputChannels : 0;
            }
            printf("\nName         = %s\n", pdi->name);
            printf("Max Inputs = %d ", pdi->maxInputChannels);
            printf("Max Outputs = %d\n", pdi->maxOutputChannels);
            if( pdi->numSampleRates == -1 ){
                printf("Sample Rate Range = %f to %f\n", pdi->sampleRates[0], pdi->sampleRates[1] );
            }else{
                printf("Sample Rates =");
                for(j=0; j<pdi->numSampleRates; j++){
                    printf(" %8.2f,", pdi->sampleRates[j] );
                }
                printf("\n");
            }
            
            printf("Native Sample Formats = ");
            if( pdi->nativeSampleFormats & paInt8 )        printf("paInt8, ");
            if( pdi->nativeSampleFormats & paUInt8 )       printf("paUInt8, ");
            if( pdi->nativeSampleFormats & paInt16 )       printf("paInt16, ");
            if( pdi->nativeSampleFormats & paInt32 )       printf("paInt32, ");
            if( pdi->nativeSampleFormats & paFloat32 )     printf("paFloat32, ");
            if( pdi->nativeSampleFormats & paInt24 )       printf("paInt24, ");
            if( pdi->nativeSampleFormats & paPackedInt24 ) printf("paPackedInt24, ");
            printf("\n");
        }
     
        if(err != paNoError) goto error;
        
        printf("Pa_GetDefaultOutputDeviceID()  %d\n", Pa_GetDefaultOutputDeviceID());
	printf("Pa_GetDefaultInputDeviceID()  %d\n", Pa_GetDefaultInputDeviceID());
        
        if (chan > 0) {
            driver->capture_nchannels = (driver->capture_nchannels < chan) ? driver->capture_nchannels : chan;
            driver->playback_nchannels = (driver->playback_nchannels < chan) ? driver->playback_nchannels : chan;
        }

	// JOQ: should use portaudio_driver_set_parameters(), instead
        err = Pa_OpenStream(&driver->stream,
                            ((capturing) ? Pa_GetDefaultInputDeviceID() : paNoDevice),	
                            ((capturing) ? driver->capture_nchannels : 0),             
                            paFloat32,	/* 32 bit floating point input */
                            NULL,
                            ((playing) ? Pa_GetDefaultOutputDeviceID() : paNoDevice),
                            ((playing) ?  driver->playback_nchannels : 0),        
                            paFloat32,  /* 32 bit floating point output */
                            NULL,
                            rate,
                            frames_per_cycle,            /* frames per buffer */
                            0,              /* number of buffers, if zero then use default minimum */
                            paClipOff,      /* we won't output out of range samples so don't bother clipping them */
                            paCallback,
                            driver);
    
        if (err != paNoError) goto error;
        
        driver->client = client; 
        driver->period_usecs = (((float) driver->frames_per_cycle) / driver->frame_rate) * 1000000.0f;
        
        jack_init_time();
         
        return((jack_driver_t *) driver);
    
error:
    
        Pa_Terminate();
        fprintf(stderr, "An error occured while using the portaudio stream\n"); 
        fprintf(stderr, "Error number: %d\n", err);
        fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
        free(driver);
        return NULL;
}

/** free all memory allocated by a driver instance
 */
static void
portaudio_driver_delete (portaudio_driver_t *driver)
{
        /* Close PortAudio stream and terminate */
        Pa_CloseStream(driver->stream);
        Pa_Terminate();
	free(driver);
}

//== driver "plugin" interface =================================================

/* DRIVER "PLUGIN" INTERFACE */

jack_driver_desc_t *
driver_get_descriptor ()
{
	jack_driver_desc_t * desc;
	unsigned int i;
	desc = calloc (1, sizeof (jack_driver_desc_t));

	strcpy (desc->name, "portaudio");
	desc->nparams = 7;
	desc->params = calloc (desc->nparams,
			       sizeof (jack_driver_param_desc_t));

	i = 0;
	strcpy (desc->params[i].name, "channel");
	desc->params[i].character  = 'c';
	desc->params[i].type       = JackDriverParamInt;
	desc->params[i].value.ui   = 0;
	strcpy (desc->params[i].short_desc, "Maximium number of channels");
	strcpy (desc->params[i].long_desc, desc->params[i].short_desc);

	i++;
	strcpy (desc->params[i].name, "capture");
	desc->params[i].character  = 'C';
	desc->params[i].type       = JackDriverParamBool;
	desc->params[i].value.i    = TRUE;
	strcpy (desc->params[i].short_desc, "Whether or not to capture");
	strcpy (desc->params[i].long_desc, desc->params[i].short_desc);

	i++;
	strcpy (desc->params[i].name, "playback");
	desc->params[i].character  = 'P';
	desc->params[i].type       = JackDriverParamBool;
	desc->params[i].value.i    = TRUE;
	strcpy (desc->params[i].short_desc, "Whether or not to playback");
	strcpy (desc->params[i].long_desc, desc->params[i].short_desc);

	i++;
	strcpy (desc->params[i].name, "duplex");
	desc->params[i].character  = 'D';
	desc->params[i].type       = JackDriverParamBool;
	desc->params[i].value.i    = TRUE;
	strcpy (desc->params[i].short_desc, "Capture and playback");
	strcpy (desc->params[i].long_desc, desc->params[i].short_desc);

	i++;
	strcpy (desc->params[i].name, "rate");
	desc->params[i].character  = 'r';
	desc->params[i].type       = JackDriverParamUInt;
	desc->params[i].value.ui   = 48000U;
	strcpy (desc->params[i].short_desc, "Sample rate");
	strcpy (desc->params[i].long_desc, desc->params[i].short_desc);

	i++;
	strcpy (desc->params[i].name, "period");
	desc->params[i].character  = 'p';
	desc->params[i].type       = JackDriverParamUInt;
	desc->params[i].value.ui   = 128U;
	strcpy (desc->params[i].short_desc, "Frames per period");
	strcpy (desc->params[i].long_desc, desc->params[i].short_desc);

	i++;
	strcpy (desc->params[i].name, "dither");
	desc->params[i].character  = 'z';
	desc->params[i].type       = JackDriverParamChar;
	desc->params[i].value.c    = '-';
	strcpy (desc->params[i].short_desc, "Dithering mode");
	strcpy (desc->params[i].long_desc,
		"  Dithering Mode:\n"
		"    r : rectangular\n"
		"    t : triangular\n"
		"    s : shaped\n"
		"    - : no dithering");


	return desc;
}

const char driver_client_name[] = "portaudio";

jack_driver_t *
driver_initialize (jack_client_t *client, const JSList * params)
{
	jack_nframes_t srate = 48000;
	jack_nframes_t frames_per_interrupt = 1024;
	int capture = FALSE;
	int playback = FALSE;
        int chan = -1;
	DitherAlgorithm dither = None;
	const JSList * node;
	const jack_driver_param_t * param;
   

	for (node = params; node; node = jack_slist_next (node)) {
		param = (const jack_driver_param_t *) node->data;

		switch (param->character) {
                        
		case 'D':
			capture = TRUE;
			playback = TRUE;
			break;
                                    
		case 'c':
			chan = (int) param->value.ui;
			break;
    
		case 'C':
			capture = TRUE;
			break;
    
		case 'P':
			playback = TRUE;
			break;

		case 'r':
			srate = param->value.ui;
			break;
                                    
		case 'p':
			frames_per_interrupt = (unsigned int) param->value.ui;
			break;
                                    
		case 'z':
			switch ((int) param->value.c) {
			case '-':
				dither = None;
				break;
    
			case 'r':
				dither = Rectangular;
				break;
    
			case 's':
				dither = Shaped;
				break;
    
			case 't':
			default:
				dither = Triangular;
				break;
			}
			break;
		}
	}

        /* duplex is the default */
	if (!capture && !playback) {
		capture = TRUE;
		playback = TRUE;
	}

	return portaudio_driver_new ("portaudio", client, frames_per_interrupt,
				     srate, capture, playback, chan, dither);
}

void
driver_finish (jack_driver_t *driver)
{
	portaudio_driver_delete ((portaudio_driver_t *) driver);
}

