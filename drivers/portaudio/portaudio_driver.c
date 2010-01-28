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
    
    02-09-03 : Modify jack port naming : add the name of the used driver
               Add the -n option to load a specific driver using it's name
    04-09-03 : Correct bug in -n option management : input and ouput have to be treated separately
    08-09-03 : More robust driver loading code : new portaudio_load_default and portaudio_load_driver functions.
    24-09-03 : Does not tries to load default device if the required one is not found, returns and error.
    14-10-03 : After jack port name size extension, does not use fixed length for CoreAudio driver name anymore
	09-01-04 : Handle different value for channel in and channel out (using -i and -o)
	12-01-04 : Connect port names (inverse "in" and "out")    	
	13-01-04 : Correct the length of jack port : use JACK_PORT_NAME_SIZE
	22-03-04 : Remove jack_init_time, rename input and ouput ports using "capture" and "playback" 
    10-11-04 : S. Letz: Add management of -I option for use with JackPilot.
	17-11-04 : S. Letz: Better debug code.
	03-02-05 : S. Letz: fix several driver detection bugs on OSX.
	06-08-05 : S.Letz: Remove the "-I" parameter, change the semantic of "-n" parameter on OSX : -n (driver name) now correctly uses the PropertyDeviceUID
			  (persistent accross reboot...) as the identifier for the used coreaudio driver.

*/

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>

#include <jack/engine.h>
#include "portaudio_driver.h"

#ifdef JACK_USE_MACH_THREADS

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CFString.h>

static OSStatus get_device_name_from_id(AudioDeviceID id, char name[60])
{
    UInt32 size = sizeof(char) * 60;
	OSStatus stat = AudioDeviceGetProperty(id, 0, false,
					   kAudioDevicePropertyDeviceName,
					   &size,
					   &name[0]);
    return stat;
}

static OSStatus get_device_id_from_num(int i, AudioDeviceID * id)
{
    OSStatus theStatus;
    UInt32 theSize;
    int nDevices;
    AudioDeviceID *theDeviceList;

    theStatus =
	AudioHardwareGetPropertyInfo(kAudioHardwarePropertyDevices,
				     &theSize, NULL);
    nDevices = theSize / sizeof(AudioDeviceID);
    theDeviceList =
	(AudioDeviceID *) malloc(nDevices * sizeof(AudioDeviceID));
    theStatus =
	AudioHardwareGetProperty(kAudioHardwarePropertyDevices, &theSize,
				 theDeviceList);

    *id = theDeviceList[i];
    return theStatus;
}

static OSStatus get_device_id_from_uid(char* UID, AudioDeviceID* id)
{
	UInt32 size = sizeof(AudioValueTranslation);
	CFStringRef inIUD = CFStringCreateWithCString(NULL, UID, CFStringGetSystemEncoding());
	AudioValueTranslation value = { &inIUD, sizeof(CFStringRef), id, sizeof(AudioDeviceID) };
	if (inIUD == NULL) {
		return kAudioHardwareUnspecifiedError;
	} else {
		OSStatus res = AudioHardwareGetProperty(kAudioHardwarePropertyDeviceForUID, &size, &value);
		CFRelease(inIUD);
		return res;
	}
}

#else
typedef unsigned int AudioDeviceID;
#endif

#define PRINTDEBUG 1

void PALog(char *fmt, ...)
{
#ifdef PRINTDEBUG
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "JCA: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
#endif
}

static int
paCallback(void *inputBuffer, void *outputBuffer,
                             unsigned long framesPerBuffer,
                             PaTimestamp outTime, void *userData)
{
	portaudio_driver_t * driver = (portaudio_driver_t*)userData;

	driver->inPortAudio = (float*)inputBuffer;
	driver->outPortAudio = (float*)outputBuffer;
	driver->last_wait_ust = jack_get_microseconds();
	return driver->engine->run_cycle(driver->engine, framesPerBuffer, 0);
}

static int
portaudio_driver_attach (portaudio_driver_t *driver, jack_engine_t *engine)
{
	jack_port_t *port;
	int port_flags;
	channel_t chn;
	char buf[JACK_PORT_NAME_SIZE];
		
	driver->engine = engine;
	
	if (driver->engine->set_buffer_size (engine, driver->frames_per_cycle)) {
		jack_error ("portaudio: cannot set engine buffer size to %d (check MIDI)", driver->frames_per_cycle);
		return -1;
	}
	driver->engine->set_sample_rate (engine, driver->frame_rate);
	
	port_flags = JackPortIsOutput|JackPortIsPhysical|JackPortIsTerminal;

	/*
	if (driver->has_hw_monitoring) {
			port_flags |= JackPortCanMonitor;
	}
	*/

	for (chn = 0; chn < driver->capture_nchannels; chn++) {
			//snprintf (buf, sizeof(buf) - 1, "%s:capture%lu", driver->driver_name, chn+1);
			snprintf (buf, sizeof(buf) - 1, "%s:out%lu", driver->driver_name, chn+1);

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
			//snprintf (buf, sizeof(buf) - 1, "%s:playback%lu", driver->driver_name, chn+1);
			snprintf (buf, sizeof(buf) - 1, "%s:in%lu", driver->driver_name, chn+1);

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
	int i,bytes = nframes*sizeof(float);
	
	/* Clear in case of nothing is connected */
	memset(driver->outPortAudio, 0, driver->playback_nchannels*bytes);
			
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
		0,				/* number of buffers = default min */
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
			if (driver->engine->set_buffer_size (
				    driver->engine, driver->frames_per_cycle)) {
				jack_error ("portaudio: cannot set engine buffer size to %d (check MIDI)", driver->frames_per_cycle);
				return -1;
			}
				
		}
		return 0;

	} else { 

		// JOQ: this driver is dead.  How do we terminate it?
		Pa_Terminate();
		jack_error("Unable to set portaudio parameters");
		jack_error("Error number: %d", err);
		jack_error("Error message: %s", Pa_GetErrorText(err));
		return EIO;
	}
}

static int
portaudio_driver_reset_parameters (portaudio_driver_t* driver,
				   jack_nframes_t nframes,
				   jack_nframes_t rate)
{
	if (!jack_power_of_two(nframes)) {
		jack_error("PA: frames must be a power of two "
			 "(64, 512, 1024, ...)");
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

static int portaudio_load_default (portaudio_driver_t *driver, 
                                    int numDevices, 
                                    int capturing, 
                                    int playing, 
                                    int* inputDeviceID,
                                    int* outputDeviceID)
{
    const PaDeviceInfo *pdi;
    int i,j;
    int found = 0;
    
    PALog("Look for default driver\n");
    
    *inputDeviceID = Pa_GetDefaultInputDeviceID();
    *outputDeviceID = Pa_GetDefaultOutputDeviceID();

    for(i=0; i<numDevices; i++)
    {
        pdi = Pa_GetDeviceInfo(i);
        PALog("---------------------------------------------- #%d\n", i);
        
        if (i == Pa_GetDefaultInputDeviceID()) {
            driver->capture_nchannels = (capturing) ? pdi->maxInputChannels : 0;
            strcpy (driver->driver_name,pdi->name);
            found = 1;
        }
        
        if (i == Pa_GetDefaultOutputDeviceID()){
            driver->playback_nchannels = (playing) ? pdi->maxOutputChannels : 0;
            strcpy (driver->driver_name,pdi->name);
            found = 1;
        }
        
        PALog("\nName         = %s\n", pdi->name);
        PALog("Max Inputs = %d ", pdi->maxInputChannels);
        PALog("Max Outputs = %d\n", pdi->maxOutputChannels);
        if( pdi->numSampleRates == -1 ){
            PALog("Sample Rate Range = %f to %f\n", pdi->sampleRates[0], pdi->sampleRates[1]);
        }else{
            PALog("Sample Rates =");
            for(j=0; j<pdi->numSampleRates; j++){
                PALog(" %8.2f,", pdi->sampleRates[j]);
            }
            PALog("\n");
        }
        
        PALog("Native Sample Formats = ");
        if (pdi->nativeSampleFormats & paInt8)        PALog("paInt8, ");
        if (pdi->nativeSampleFormats & paUInt8)       PALog("paUInt8, ");
        if (pdi->nativeSampleFormats & paInt16)       PALog("paInt16, ");
        if (pdi->nativeSampleFormats & paInt32)       PALog("paInt32, ");
        if (pdi->nativeSampleFormats & paFloat32)     PALog("paFloat32, ");
        if (pdi->nativeSampleFormats & paInt24)       PALog("paInt24, ");
        if (pdi->nativeSampleFormats & paPackedInt24) PALog("paPackedInt24, ");
        PALog("\n");
    }
    
    return found;
}

static int portaudio_load_driver (portaudio_driver_t *driver, 
                                    int numDevices, 
                                    int capturing, 
                                    int playing, 
                                    int* inputDeviceID,
                                    int* outputDeviceID,
                                    char* driver_name)
{
    const PaDeviceInfo *pdi;
    int found = 0;
    int i,j;
    
    PALog("Look for %s driver\n",driver_name);
      
    for(i=0; i<numDevices; i++)
    {
        pdi = Pa_GetDeviceInfo(i);
        PALog("---------------------------------------------- #%d\n", i);
        
		// compare the first character
		if (strncmp (driver_name, pdi->name,
		     JACK_DRIVER_PARAM_STRING_MAX) == 0) {
		if (pdi->maxInputChannels > 0) {
			*inputDeviceID = i;
			driver->capture_nchannels =
			(capturing) ? pdi->maxInputChannels : 0;
			strcpy(driver->driver_name,pdi->name);
			PALog("Found input driver = %s\n", driver_name);
			found = 1;
		}
	    if (pdi->maxOutputChannels > 0) {
			*outputDeviceID = i;
			driver->playback_nchannels =
			(playing) ? pdi->maxOutputChannels : 0;
			strcpy (driver->driver_name,pdi->name);
			PALog("Found output driver = %s\n", driver_name);
			found = 1;
		} else {
			PALog("Found driver without input or ouput = %s\n",
		       driver_name);
            }
        }
                        
        PALog("\nName         = %s\n", pdi->name);
        PALog("Max Inputs = %d ", pdi->maxInputChannels);
        PALog("Max Outputs = %d\n", pdi->maxOutputChannels);
        if( pdi->numSampleRates == -1 ){
            PALog("Sample Rate Range = %f to %f\n", pdi->sampleRates[0],
			pdi->sampleRates[1]);
        }else{
            PALog("Sample Rates =");
            for(j=0; j<pdi->numSampleRates; j++){
                PALog(" %8.2f,", pdi->sampleRates[j]);
            }
            PALog("\n");
        }
        
        PALog("Native Sample Formats = ");
        if (pdi->nativeSampleFormats & paInt8)        PALog("paInt8, ");
        if (pdi->nativeSampleFormats & paUInt8)       PALog("paUInt8, ");
        if (pdi->nativeSampleFormats & paInt16)       PALog("paInt16, ");
        if (pdi->nativeSampleFormats & paInt32)       PALog("paInt32, ");
        if (pdi->nativeSampleFormats & paFloat32)     PALog("paFloat32, ");
        if (pdi->nativeSampleFormats & paInt24)       PALog("paInt24, ");
        if (pdi->nativeSampleFormats & paPackedInt24) PALog("paPackedInt24, ");
        PALog("\n");
    }
    
    return found;
}

/** create a new driver instance
 */
static jack_driver_t *
portaudio_driver_new (char *name, 
				jack_client_t* client,
				jack_nframes_t frames_per_cycle,
				jack_nframes_t rate,
				int capturing,
				int playing,
				int chan_in, 
				int chan_out,
				DitherAlgorithm dither,
				char* driver_name)
{
	portaudio_driver_t *driver;
	PaError err = paNoError;
	int numDevices;
	int inputDeviceID,outputDeviceID;
	int found;
	
	PALog("portaudio driver version : %d\n", kVersion);
	PALog("creating portaudio driver ... %" PRIu32 "|%" PRIu32 "\n",
		frames_per_cycle, rate);

	driver = (portaudio_driver_t *) calloc (1, sizeof (portaudio_driver_t));

	jack_driver_init ((jack_driver_t *) driver);

	if (!jack_power_of_two(frames_per_cycle)) {
		jack_error ("PA: -p must be a power of two.");
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
	driver->stream = NULL;

#ifdef JACK_USE_MACH_THREADS	
	AudioDeviceID device_id;
	if (driver_name) {
		if (get_device_id_from_uid(driver_name, &device_id) != noErr)
			goto error;
		if (get_device_name_from_id(device_id, driver->driver_name) != noErr)
			goto error;
	} else {
		if (get_device_id_from_num(0, &device_id) != noErr)
			goto error; 
		if (get_device_name_from_id(device_id, driver->driver_name) != noErr)
			goto error;
	}
#endif
       
	err = Pa_Initialize();
	PALog("Pa_Initialize OK \n");
	
	PALog("Driver name required %s\n",driver->driver_name);
	numDevices = Pa_CountDevices();
	
	if( numDevices < 0 ){
		PALog("ERROR: Pa_CountDevices returned 0x%x\n", numDevices);
		err = numDevices;
		goto error;
	}
	
	PALog("Number of devices = %d\n", numDevices);

	if (strcmp(driver->driver_name,"") == 0) {
		found = portaudio_load_default(driver,numDevices,capturing,playing,&inputDeviceID,&outputDeviceID);
		if (!found) {
			PALog("ERROR : default driver has not been found\n");
			err = paHostError;
			goto error;
		}
	}else{
		found = portaudio_load_driver(driver,numDevices,capturing,playing,&inputDeviceID,&outputDeviceID,driver->driver_name);
		if (!found) {
			 PALog("ERROR : driver %s has not been found \n",driver->driver_name);
			 err = paHostError;
			 goto error;
		}
	}

	if (err != paNoError) goto error;
	
	PALog("Pa_GetDefaultOutputDeviceID() %ld\n", (long)Pa_GetDefaultOutputDeviceID());
	PALog("Pa_GetDefaultInputDeviceID() %ld\n",  (long)Pa_GetDefaultInputDeviceID());
	
	PALog("--------------------------------------------------\n");
	PALog("CoreAudio driver %s will be loaded\n", driver->driver_name);
	PALog("inputDeviceID %ld\n",  (long)inputDeviceID);
	PALog("outputDeviceID %ld\n",  (long)outputDeviceID);
	PALog("driver->capture_nchannels %ld\n", driver->capture_nchannels);
	PALog("driver->playback_nchannels %ld\n", driver->playback_nchannels);
	
	PALog("chan_in, chan_out %ld %ld\n",  (long)chan_in,  (long)chan_out);
        
	if (chan_in > 0) 
		driver->capture_nchannels = (driver->capture_nchannels < chan_in) ? driver->capture_nchannels : chan_in;
	
	if (chan_out > 0) 
		driver->playback_nchannels = (driver->playback_nchannels < chan_out) ? driver->playback_nchannels : chan_out;
		
	PALog("driver->capture_nchannels %ld\n", driver->capture_nchannels);
	PALog("driver->playback_nchannels %ld\n", driver->playback_nchannels);
	
	err = Pa_OpenStream(&driver->stream,
						((capturing && (driver->capture_nchannels > 0)) ? inputDeviceID : paNoDevice),
						((capturing) ? driver->capture_nchannels : 0),             
						paFloat32,	// 32 bit floating point input 
						NULL,
						((playing && (driver->playback_nchannels > 0)) ? outputDeviceID : paNoDevice),
						((playing) ? driver->playback_nchannels : 0),        
						paFloat32,  // 32 bit floating point output 
						NULL,
						rate,
						frames_per_cycle,            // frames per buffer 
						0,              // number of buffers, if zero then use default minimum 
						paClipOff,      // we won't output out of range samples so don't bother clipping them 
						paCallback,
						driver);
	
	if (err != paNoError) goto error;
	
	driver->client = client; 
	driver->period_usecs = (((float) driver->frames_per_cycle) / driver->frame_rate) * 1000000.0f;
	return((jack_driver_t *) driver);

error:

	Pa_Terminate();
	jack_error("An error occured while using the portaudio stream");
	jack_error("Error number: %d", err);
	jack_error("Error message: %s", Pa_GetErrorText(err));
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
	desc->nparams = 10;
	desc->params = calloc (desc->nparams,
			       sizeof (jack_driver_param_desc_t));

	i = 0;
	strcpy (desc->params[i].name, "channel");
	desc->params[i].character  = 'c';
	desc->params[i].type       = JackDriverParamInt;
	desc->params[i].value.ui   = 0;
	strcpy (desc->params[i].short_desc, "Maximum number of channels");
	strcpy (desc->params[i].long_desc, desc->params[i].short_desc);
	
	i++;
	strcpy (desc->params[i].name, "channelin");
	desc->params[i].character  = 'i';
	desc->params[i].type       = JackDriverParamInt;
	desc->params[i].value.ui   = 0;
	strcpy (desc->params[i].short_desc, "Maximum number of input channels");
	strcpy (desc->params[i].long_desc, desc->params[i].short_desc);

	i++;
	strcpy (desc->params[i].name, "channelout");
	desc->params[i].character  = 'o';
	desc->params[i].type       = JackDriverParamInt;
	desc->params[i].value.ui   = 0;
	strcpy (desc->params[i].short_desc, "Maximum number of ouput channels");
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
	desc->params[i].value.ui   = 1024U;
	strcpy (desc->params[i].short_desc, "Frames per period");
	strcpy (desc->params[i].long_desc, desc->params[i].short_desc);
	
	i++;
	strcpy (desc->params[i].name, "name");
	desc->params[i].character  = 'n';
	desc->params[i].type       = JackDriverParamString;
	desc->params[i].value.ui   = 128U;
	strcpy (desc->params[i].short_desc, "Driver name");
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
	AudioDeviceID deviceID = 0;
	int capture = FALSE;
	int playback = FALSE;
	int chan_in = -1;
	int chan_out = -1;
	DitherAlgorithm dither = None;
	const JSList * node;
	const jack_driver_param_t * param;
	char *name = NULL;

#ifdef JACK_USE_MACH_THREADS	
	get_device_id_from_num(0,&deviceID); // takes a default value (first device)
#endif
	
	for (node = params; node; node = jack_slist_next (node)) {
		param = (const jack_driver_param_t *) node->data;

		switch (param->character) {
                
		case 'n':
			name = (char *) param->value.str;
			PALog("Driver name found %s\n",name);
			break;
                        
		case 'D':
			capture = TRUE;
			playback = TRUE;
			break;
                                    
		case 'c':
			chan_in = (int) param->value.ui;
			chan_out = (int) param->value.ui;
			break;
			
		case 'i':
			chan_in = (int) param->value.ui;
			break;
		
		case 'o':
			chan_out = (int) param->value.ui;
			break;
    
		case 'C':
			capture = param->value.i;
			break;
    
		case 'P':
			playback = param->value.i;
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
				     srate, capture, playback, chan_in,
				     chan_out, dither, name);
}

void
driver_finish (jack_driver_t *driver)
{
	portaudio_driver_delete ((portaudio_driver_t *) driver);
}

