/*
 Copyright � Grame, 2003.
 Copyright � Johnny Petrantoni, 2003.
 
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
	
 Johnny Petrantoni, johnny@lato-b.com - Italy, Rome.
 
 Jan 30, 2004: Johnny Petrantoni: first code of the coreaudio driver, based on portaudio driver by Stephane Letz.
 Feb 02, 2004: Johnny Petrantoni: fixed null cycle, removed double copy of buffers in AudioRender, the driver works fine (tested with Built-in Audio and Hammerfall RME), but no cpu load is displayed.
 Feb 03, 2004: Johnny Petrantoni: some little fix.
 Feb 03, 2004: Stephane Letz: some fix in AudioRender.cpp code.
 Feb 03, 2004: Johnny Petrantoni: removed the default device stuff (useless, in jackosx, because JackPilot manages this behavior), the device must be specified. and all parameter must be correct.
 Feb 04, 2004: Johnny Petrantoni: now the driver supports interfaces with multiple interleaved streams (such as the MOTU 828).
 Nov 05, 2004: S.Letz: correct management of -I option for use with JackPilot.
 Nov 15, 2004: S.Letz: Set a default value for deviceID.
 Nov 30, 2004: S.Letz: In coreaudio_driver_write : clear to avoid playing dirty buffers when the client does not produce output anymore.
 Dec 05, 2004: S.Letz: XRun detection 
 Dec 09, 2004: S.Letz: Dynamic buffer size change
 Dec 23, 2004: S.Letz: Correct bug in dynamic buffer size change : update period_usecs
 Jan 20, 2005: S.Letz: Almost complete rewrite using AUHAL.
 May 20, 2005: S.Letz: Add "systemic" latencies management.
 Jun 06, 2005: S.Letz: Remove the "-I" parameter, change the semantic of "-n" parameter : -n (driver name) now correctly uses the PropertyDeviceUID
					   (persistent accross reboot...) as the identifier for the used coreaudio driver.
 Jun 14, 2005: S.Letz: Since the "-I" parameter is not used anymore, rename the "systemic" latencies management parametes "-I" and "-O" like for the ALSA driver.
 Aug 16, 2005: S.Letz: Remove get_device_id_from_num, use get_default_device instead. If the -n option is not used or the device name cannot
					   be found, the default device is used. Note: the default device can be used only if both default input and default output are the same.
 Dec 19, 2005: S.Letz: Add -d option (display_device_names).
 Apri 7, 2006: S.Letz: Synchronization with the jackdmp coreaudio driver version: improve half-duplex management.
 May 17, 2006: S.Letz: Minor fix in driver_initialize.
 May 18, 2006: S.Letz: Document sample rate default value.
 May 31, 2006: S.Letz: Apply Rui patch for more consistent driver parameter naming.
 Dec 04, 2007: S.Letz: Fix a bug in sample rate management (occuring in particular with "aggregate" devices).
 Dec 05, 2007: S.Letz: Correct sample_rate management in Open. Better handling in sample_rate change listener.
 */

#include <stdio.h>
#include <string.h>
#include <jack/engine.h>
#include "coreaudio_driver.h"

const int CAVersion = 3;

//#define PRINTDEBUG 1

static void JCALog(char *fmt, ...)
{
#ifdef PRINTDEBUG
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "JCA: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
#endif
}

static void printError(OSStatus err)
{
#ifdef DEBUG
    switch (err) {
        case kAudioHardwareNoError:
            JCALog("error code : kAudioHardwareNoError\n");
            break;
        case kAudioHardwareNotRunningError:
            JCALog("error code : kAudioHardwareNotRunningError\n");
            break;
        case kAudioHardwareUnspecifiedError:
            JCALog("error code : kAudioHardwareUnspecifiedError\n");
            break;
        case kAudioHardwareUnknownPropertyError:
            JCALog("error code : kAudioHardwareUnknownPropertyError\n");
            break;
        case kAudioHardwareBadPropertySizeError:
            JCALog("error code : kAudioHardwareBadPropertySizeError\n");
            break;
        case kAudioHardwareIllegalOperationError:
            JCALog("error code : kAudioHardwareIllegalOperationError\n");
            break;
        case kAudioHardwareBadDeviceError:
            JCALog("error code : kAudioHardwareBadDeviceError\n");
            break;
        case kAudioHardwareBadStreamError:
            JCALog("error code : kAudioHardwareBadStreamError\n");
            break;
        case kAudioDeviceUnsupportedFormatError:
            JCALog("error code : kAudioDeviceUnsupportedFormatError\n");
            break;
        case kAudioDevicePermissionsError:
            JCALog("error code : kAudioDevicePermissionsError\n");
            break;
        default:
            JCALog("error code : unknown %ld\n", err);
            break;
    }
#endif
}

static OSStatus get_device_name_from_id(AudioDeviceID id, char name[256])
{
    UInt32 size = sizeof(char) * 256;
    OSStatus res = AudioDeviceGetProperty(id, 0, false,
					   kAudioDevicePropertyDeviceName,
					   &size,
					   &name[0]);
    return res;
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
		JCALog("get_device_id_from_uid %s %ld \n", UID, *id);
		return (*id == kAudioDeviceUnknown) ? kAudioHardwareBadDeviceError : res;
	}
}

static OSStatus get_default_device(AudioDeviceID * id)
{
    OSStatus res;
    UInt32 theSize = sizeof(UInt32);
	AudioDeviceID inDefault;
	AudioDeviceID outDefault;
  
	if ((res = AudioHardwareGetProperty(kAudioHardwarePropertyDefaultInputDevice, 
					&theSize, &inDefault)) != noErr)
		return res;
	
	if ((res = AudioHardwareGetProperty(kAudioHardwarePropertyDefaultOutputDevice, 
					&theSize, &outDefault)) != noErr)
		return res;
		
	JCALog("get_default_device: input %ld output %ld\n", inDefault, outDefault);
	
	// Get the device only if default input and ouput are the same
	if (inDefault == outDefault) {
		*id = inDefault;
		return noErr;
	} else {
		jack_error("Default input and output devices are not the same !!");
		return kAudioHardwareBadDeviceError;
	}
}

static OSStatus get_default_input_device(AudioDeviceID* id)
{
    OSStatus res;
    UInt32 theSize = sizeof(UInt32);
    AudioDeviceID inDefault;
  
    if ((res = AudioHardwareGetProperty(kAudioHardwarePropertyDefaultInputDevice,
                                        &theSize, &inDefault)) != noErr)
        return res;

    JCALog("get_default_input_device: input = %ld \n", inDefault);
    *id = inDefault;
	return noErr;
}

static OSStatus get_default_output_device(AudioDeviceID* id)
{
    OSStatus res;
    UInt32 theSize = sizeof(UInt32);
    AudioDeviceID outDefault;

    if ((res = AudioHardwareGetProperty(kAudioHardwarePropertyDefaultOutputDevice,
                                        &theSize, &outDefault)) != noErr)
        return res;

    JCALog("get_default_output_device: output = %ld\n", outDefault);
	*id = outDefault;
	return noErr;
}

OSStatus get_total_channels(AudioDeviceID device, int* channelCount, bool isInput) 
{
    OSStatus			err = noErr;
    UInt32				outSize;
    Boolean				outWritable;
    AudioBufferList*	bufferList = 0;
	AudioStreamID*		streamList = 0;
    int					i, numStream;
	
	err = AudioDeviceGetPropertyInfo(device, 0, isInput, kAudioDevicePropertyStreams, &outSize, &outWritable);
	if (err == noErr) {
		streamList = (AudioStreamID*)malloc(outSize);
		numStream = outSize/sizeof(AudioStreamID);
		JCALog("get_total_channels device stream number = %ld numStream = %ld\n", device, numStream);
		err = AudioDeviceGetProperty(device, 0, isInput, kAudioDevicePropertyStreams, &outSize, streamList);
		if (err == noErr) {
			AudioStreamBasicDescription streamDesc;
			outSize = sizeof(AudioStreamBasicDescription);
			for (i = 0; i < numStream; i++) {
				err = AudioStreamGetProperty(streamList[i], 0, kAudioDevicePropertyStreamFormat, &outSize, &streamDesc);
				JCALog("get_total_channels streamDesc mFormatFlags = %ld mChannelsPerFrame = %ld\n", streamDesc.mFormatFlags, streamDesc.mChannelsPerFrame);
			}
		}
	}
	
    *channelCount = 0;
    err = AudioDeviceGetPropertyInfo(device, 0, isInput, kAudioDevicePropertyStreamConfiguration, &outSize, &outWritable);
    if (err == noErr) {
        bufferList = (AudioBufferList*)malloc(outSize);
        err = AudioDeviceGetProperty(device, 0, isInput, kAudioDevicePropertyStreamConfiguration, &outSize, bufferList);
        if (err == noErr) {								
            for (i = 0; i < bufferList->mNumberBuffers; i++) 
                *channelCount += bufferList->mBuffers[i].mNumberChannels;
        }
    }
	
	if (streamList) 
		free(streamList);
	if (bufferList) 
		free(bufferList);	
		
    return err;
}

static OSStatus display_device_names()
{
	UInt32 size;
	Boolean isWritable;
	int i, deviceNum;
	OSStatus err;
	CFStringRef UIname;
	
	err = AudioHardwareGetPropertyInfo(kAudioHardwarePropertyDevices, &size, &isWritable);
    if (err != noErr) 
		return err;
		
	deviceNum = size/sizeof(AudioDeviceID);
	AudioDeviceID devices[deviceNum];
	
	err = AudioHardwareGetProperty(kAudioHardwarePropertyDevices, &size, devices);
    if (err != noErr) 
		return err;
	
	for (i = 0; i < deviceNum; i++) {
        char device_name[256];
		char internal_name[256];
		
		size = sizeof(CFStringRef);
		UIname = NULL;
		err = AudioDeviceGetProperty(devices[i], 0, false, kAudioDevicePropertyDeviceUID, &size, &UIname);
		if (err == noErr) {
			CFStringGetCString(UIname, internal_name, 256, CFStringGetSystemEncoding());
		} else {
			goto error;
		}
		
		size = 256;
		err = AudioDeviceGetProperty(devices[i], 0, false, kAudioDevicePropertyDeviceName, &size, device_name);
		if (err != noErr) 
			return err; 
		jack_info("ICI");
		jack_info("Device name = \'%s\', internal_name = \'%s\' (to be used as -d parameter)", device_name, internal_name); 
	}
	
	return noErr;

error:
	if (UIname != NULL)
		CFRelease(UIname);
	return err;
}

static OSStatus render(void *inRefCon, 
						AudioUnitRenderActionFlags 	*ioActionFlags, 
						const AudioTimeStamp 		*inTimeStamp, 
						UInt32 						inBusNumber, 
						UInt32 						inNumberFrames, 
						AudioBufferList 			*ioData)
{
	int res, i;
	JSList *node;
	coreaudio_driver_t* ca_driver = (coreaudio_driver_t*)inRefCon;
	AudioUnitRender(ca_driver->au_hal, ioActionFlags, inTimeStamp, 1, inNumberFrames, ca_driver->input_list);
	
	if (ca_driver->xrun_detected > 0) { /* XRun was detected */
		jack_time_t current_time = jack_get_microseconds();
		ca_driver->engine->delay(ca_driver->engine, current_time - 
			(ca_driver->last_wait_ust + ca_driver->period_usecs));
		ca_driver->last_wait_ust = current_time;
		ca_driver->xrun_detected = 0;
		return 0;
    } else {
		ca_driver->last_wait_ust = jack_get_microseconds();
		ca_driver->engine->transport_cycle_start(ca_driver->engine,
					  jack_get_microseconds());
		res = ca_driver->engine->run_cycle(ca_driver->engine, inNumberFrames, 0);
	}
	
	if (ca_driver->null_cycle_occured) {
		ca_driver->null_cycle_occured = 0;
		for (i = 0; i < ca_driver->playback_nchannels; i++) {
			memset((float*)ioData->mBuffers[i].mData, 0, sizeof(float) * inNumberFrames);
		}
	} else {
		for (i = 0, node = ca_driver->playback_ports; i < ca_driver->playback_nchannels; i++, node = jack_slist_next(node)) {
			memcpy((float*)ioData->mBuffers[i].mData,
					(jack_default_audio_sample_t*)jack_port_get_buffer(((jack_port_t *) node->data), inNumberFrames), 
					sizeof(float) * inNumberFrames);
		}
	}
	
	return res;
}

static OSStatus render_input(void *inRefCon, 
							AudioUnitRenderActionFlags 	*ioActionFlags, 
							const AudioTimeStamp 		*inTimeStamp, 
							UInt32 						inBusNumber, 
							UInt32 						inNumberFrames, 
							AudioBufferList 			*ioData)

{
	coreaudio_driver_t* ca_driver = (coreaudio_driver_t*)inRefCon;
	AudioUnitRender(ca_driver->au_hal, ioActionFlags, inTimeStamp, 1, inNumberFrames, ca_driver->input_list);
	if (ca_driver->xrun_detected > 0) { /* XRun was detected */
		jack_time_t current_time = jack_get_microseconds();
		ca_driver->engine->delay(ca_driver->engine, current_time - 
			(ca_driver->last_wait_ust + ca_driver->period_usecs));
		ca_driver->last_wait_ust = current_time;
		ca_driver->xrun_detected = 0;
		return 0;
    } else {
		ca_driver->last_wait_ust = jack_get_microseconds();
		ca_driver->engine->transport_cycle_start(ca_driver->engine,
					  jack_get_microseconds());
		return ca_driver->engine->run_cycle(ca_driver->engine, inNumberFrames, 0);
	}
}


static OSStatus sr_notification(AudioDeviceID inDevice,
        UInt32 inChannel,
        Boolean	isInput,
        AudioDevicePropertyID inPropertyID,
        void* inClientData)
{
	coreaudio_driver_t* driver = (coreaudio_driver_t*)inClientData;
	
	switch (inPropertyID) {

		case kAudioDevicePropertyNominalSampleRate: {
			JCALog("JackCoreAudioDriver::SRNotificationCallback kAudioDevicePropertyNominalSampleRate \n");
			driver->state = 1;
			break;
		}
	}
	
	return noErr;
}

static OSStatus notification(AudioDeviceID inDevice,
							UInt32 inChannel,
							Boolean	isInput,
							AudioDevicePropertyID inPropertyID,
							void* inClientData)
{
    coreaudio_driver_t* driver = (coreaudio_driver_t*)inClientData;
    switch (inPropertyID) {
	
		case kAudioDeviceProcessorOverload:
			driver->xrun_detected = 1;
			break;
			
		case kAudioDevicePropertyNominalSampleRate: {
			UInt32 outSize =  sizeof(Float64);
			Float64 sampleRate;
			AudioStreamBasicDescription srcFormat, dstFormat;
			OSStatus err = AudioDeviceGetProperty(driver->device_id, 0, kAudioDeviceSectionGlobal, kAudioDevicePropertyNominalSampleRate, &outSize, &sampleRate);
			if (err != noErr) {
				jack_error("Cannot get current sample rate");
				return kAudioHardwareUnsupportedOperationError;
			}
			JCALog("JackCoreAudioDriver::NotificationCallback kAudioDevicePropertyNominalSampleRate %ld\n", (long)sampleRate);
			outSize = sizeof(AudioStreamBasicDescription);
			
			// Update SR for input
			err = AudioUnitGetProperty(driver->au_hal, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &srcFormat, &outSize);
			if (err != noErr) {
				jack_error("Error calling AudioUnitSetProperty - kAudioUnitProperty_StreamFormat kAudioUnitScope_Input");
			}
			srcFormat.mSampleRate = sampleRate;
			err = AudioUnitSetProperty(driver->au_hal, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &srcFormat, outSize);
			if (err != noErr) {
				jack_error("Error calling AudioUnitSetProperty - kAudioUnitProperty_StreamFormat kAudioUnitScope_Input");
			}
		
			// Update SR for output
			err = AudioUnitGetProperty(driver->au_hal, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &dstFormat, &outSize);
			if (err != noErr) {
				jack_error("Error calling AudioUnitSetProperty - kAudioUnitProperty_StreamFormat kAudioUnitScope_Output");
			}
			dstFormat.mSampleRate = sampleRate;
			err = AudioUnitSetProperty(driver->au_hal, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &dstFormat, outSize);
			if (err != noErr) {
				jack_error("Error calling AudioUnitSetProperty - kAudioUnitProperty_StreamFormat kAudioUnitScope_Output");
			}
			break;
		}
    }
    return noErr;
}

static int
coreaudio_driver_attach(coreaudio_driver_t * driver, jack_engine_t * engine)
{
    jack_port_t *port;
	JSList *node;
    int port_flags;
    channel_t chn;
    char buf[JACK_PORT_NAME_SIZE];
	char channel_name[64];
	OSStatus err;
	UInt32 size;
	UInt32 value1,value2;
    Boolean isWritable;
	
    driver->engine = engine;

    if (driver->engine->set_buffer_size(engine, driver->frames_per_cycle)) {
	    jack_error ("coreaudio: cannot set engine buffer size to %d (check MIDI)", driver->frames_per_cycle);
	    return -1;
    }
    driver->engine->set_sample_rate(engine, driver->frame_rate);

    port_flags = JackPortIsOutput | JackPortIsPhysical | JackPortIsTerminal;

    /*
       if (driver->has_hw_monitoring) {
			port_flags |= JackPortCanMonitor;
       }
	*/

    for (chn = 0; chn < driver->capture_nchannels; chn++) {
		err = AudioDeviceGetPropertyInfo(driver->device_id, chn + 1, true, kAudioDevicePropertyChannelName, &size, &isWritable);
		if (err == noErr && size > 0)  {
			err = AudioDeviceGetProperty(driver->device_id, chn + 1, true, kAudioDevicePropertyChannelName, &size, channel_name);	
			if (err != noErr) 
				JCALog("AudioDeviceGetProperty kAudioDevicePropertyChannelName error \n");
			snprintf(buf, sizeof(buf) - 1, "%s:out_%s%lu", driver->capture_driver_name, channel_name, chn + 1);
		} else {
			snprintf(buf, sizeof(buf) - 1, "%s:out%lu", driver->capture_driver_name, chn + 1);
		}
	
		if ((port = jack_port_register(driver->client, buf,
					JACK_DEFAULT_AUDIO_TYPE, port_flags,
					0)) == NULL) {
			jack_error("coreaudio: cannot register port for %s", buf);
			break;
		}

		size = sizeof(UInt32);
		value1 = value2 = 0;
		err = AudioDeviceGetProperty(driver->device_id, 0, true, kAudioDevicePropertyLatency, &size, &value1);	
		if (err != noErr) 
			JCALog("AudioDeviceGetProperty kAudioDevicePropertyLatency error \n");
		err = AudioDeviceGetProperty(driver->device_id, 0, true, kAudioDevicePropertySafetyOffset, &size, &value2);	
		if (err != noErr) 
			JCALog("AudioDeviceGetProperty kAudioDevicePropertySafetyOffset error \n");
		
		jack_port_set_latency(port, driver->frames_per_cycle + value1 + value2 + driver->capture_frame_latency);
		driver->capture_ports =
			jack_slist_append(driver->capture_ports, port);
    }

    port_flags = JackPortIsInput | JackPortIsPhysical | JackPortIsTerminal;

	for (chn = 0; chn < driver->playback_nchannels; chn++) {
		err = AudioDeviceGetPropertyInfo(driver->device_id, chn + 1, false, kAudioDevicePropertyChannelName, &size, &isWritable);
		if (err == noErr && size > 0)  {
			err = AudioDeviceGetProperty(driver->device_id, chn + 1, false, kAudioDevicePropertyChannelName, &size, channel_name);	
			if (err != noErr) 
				JCALog("AudioDeviceGetProperty kAudioDevicePropertyChannelName error \n");
			snprintf(buf, sizeof(buf) - 1, "%s:in_%s%lu", driver->playback_driver_name, channel_name, chn + 1);
		} else {
			snprintf(buf, sizeof(buf) - 1, "%s:in%lu", driver->playback_driver_name, chn + 1);
		}

		if ((port = jack_port_register(driver->client, buf,
					JACK_DEFAULT_AUDIO_TYPE, port_flags,
					0)) == NULL) {
			jack_error("coreaudio: cannot register port for %s", buf);
			break;
		}

		size = sizeof(UInt32);
		value1 = value2 = 0;
		err = AudioDeviceGetProperty(driver->device_id, 0, false, kAudioDevicePropertyLatency, &size, &value1);	
		if (err != noErr) 
			JCALog("AudioDeviceGetProperty kAudioDevicePropertyLatency error \n");
		err = AudioDeviceGetProperty(driver->device_id, 0, false, kAudioDevicePropertySafetyOffset, &size, &value2);	
		if (err != noErr) 
			JCALog("AudioDeviceGetProperty kAudioDevicePropertySafetyOffset error \n");
	
		jack_port_set_latency(port, driver->frames_per_cycle + value1 + value2 + driver->playback_frame_latency);
		driver->playback_ports =
			jack_slist_append(driver->playback_ports, port);
	}
	
	// Input buffers do no change : prepare them only once
	for (chn = 0, node = driver->capture_ports; chn < driver->capture_nchannels; chn++, node = jack_slist_next(node)) {
		driver->input_list->mBuffers[chn].mData 
			= (jack_default_audio_sample_t*)jack_port_get_buffer(((jack_port_t *) node->data), driver->frames_per_cycle);
    }

    jack_activate(driver->client);
    return 0;
}

static int
coreaudio_driver_detach(coreaudio_driver_t * driver, jack_engine_t * engine)
{
    JSList *node;

    if (driver->engine == 0) {
		return -1;
    }

    for (node = driver->capture_ports; node; node = jack_slist_next(node)) {
		jack_port_unregister(driver->client, ((jack_port_t *) node->data));
    }

    jack_slist_free(driver->capture_ports);
    driver->capture_ports = 0;

    for (node = driver->playback_ports; node; node = jack_slist_next(node)) {
		jack_port_unregister(driver->client, ((jack_port_t *) node->data));
    }

    jack_slist_free(driver->playback_ports);
    driver->playback_ports = 0;

    driver->engine = 0;
    return 0;
}

static int
coreaudio_driver_null_cycle(coreaudio_driver_t * driver, jack_nframes_t nframes)
{
	driver->null_cycle_occured = 1;
    return 0;
}

static int
coreaudio_driver_read(coreaudio_driver_t * driver, jack_nframes_t nframes)
{
    return 0;
}

static int
coreaudio_driver_write(coreaudio_driver_t * driver, jack_nframes_t nframes)
{
	return 0;
}

static int coreaudio_driver_audio_start(coreaudio_driver_t * driver)
{
	return (AudioOutputUnitStart(driver->au_hal) == noErr) ? 0 : -1;
}

static int coreaudio_driver_audio_stop(coreaudio_driver_t * driver)
{
	return (AudioOutputUnitStop(driver->au_hal) == noErr) ? 0 : -1;
}

static int
coreaudio_driver_bufsize(coreaudio_driver_t * driver,
			 jack_nframes_t nframes)
{

    /* This gets called from the engine server thread, so it must
     * be serialized with the driver thread. Stopping the audio
     * also stops that thread. */
	/*
	TO DO
	*/
	return 0;
}

/** create a new driver instance
*/
static jack_driver_t *coreaudio_driver_new(char* name,
										   jack_client_t* client,
										   jack_nframes_t nframes,
										   jack_nframes_t samplerate,
										   int capturing,
										   int playing,
										   int inchannels,
										   int outchannels,
										   char* capture_driver_uid,
										   char* playback_driver_uid,
										   jack_nframes_t capture_latency, 
										   jack_nframes_t playback_latency)
{
    coreaudio_driver_t *driver;
	OSStatus err = noErr;
	ComponentResult err1;
    UInt32 outSize;
	UInt32 enableIO;
	AudioStreamBasicDescription srcFormat, dstFormat;
	Float64 sampleRate;
	int in_nChannels = 0;
	int out_nChannels = 0;
	int i;
	
    driver = (coreaudio_driver_t *) calloc(1, sizeof(coreaudio_driver_t));
    jack_driver_init((jack_driver_t *) driver);

    if (!jack_power_of_two(nframes)) {
		jack_error("CA: -p must be a power of two.");
		goto error;
    }

	driver->state = 0;
    driver->frames_per_cycle = nframes;
    driver->frame_rate = samplerate;
    driver->capturing = capturing;
    driver->playing = playing;
	driver->xrun_detected = 0;
	driver->null_cycle = 0;

    driver->attach = (JackDriverAttachFunction) coreaudio_driver_attach;
    driver->detach = (JackDriverDetachFunction) coreaudio_driver_detach;
    driver->read = (JackDriverReadFunction) coreaudio_driver_read;
    driver->write = (JackDriverReadFunction) coreaudio_driver_write;
    driver->null_cycle =
	(JackDriverNullCycleFunction) coreaudio_driver_null_cycle;
    driver->bufsize = (JackDriverBufSizeFunction) coreaudio_driver_bufsize;
    driver->start = (JackDriverStartFunction) coreaudio_driver_audio_start;
    driver->stop = (JackDriverStopFunction) coreaudio_driver_audio_stop;
	driver->capture_frame_latency = capture_latency;
	driver->playback_frame_latency = playback_latency;
	
	// Duplex
    if (strcmp(capture_driver_uid, "") != 0 && strcmp(playback_driver_uid, "") != 0) {
		JCALog("Open duplex \n");
        if (get_device_id_from_uid(playback_driver_uid, &driver->device_id) != noErr) {
            if (get_default_device(&driver->device_id) != noErr) {
				jack_error("Cannot open default device");
				goto error;
			}
		}
		if (get_device_name_from_id(driver->device_id, driver->capture_driver_name) != noErr || get_device_name_from_id(driver->device_id, driver->playback_driver_name) != noErr) {
			jack_error("Cannot get device name from device ID");
			goto error;
		}
		
	// Capture only
	} else if (strcmp(capture_driver_uid, "") != 0) {
		JCALog("Open capture only \n");
		if (get_device_id_from_uid(capture_driver_uid, &driver->device_id) != noErr) {
            if (get_default_input_device(&driver->device_id) != noErr) {
				jack_error("Cannot open default device");
                goto error;
			}
		}
		if (get_device_name_from_id(driver->device_id, driver->capture_driver_name) != noErr) {
			jack_error("Cannot get device name from device ID");
			goto error;
		}
		
  	// Playback only
	} else if (playback_driver_uid != NULL) {
		JCALog("Open playback only \n");
		if (get_device_id_from_uid(playback_driver_uid, &driver->device_id) != noErr) {
            if (get_default_output_device(&driver->device_id) != noErr) {
				jack_error("Cannot open default device");
                goto error;
			}
        }
		if (get_device_name_from_id(driver->device_id, driver->playback_driver_name) != noErr) {
			jack_error("Cannot get device name from device ID");
			goto error;
		}
		
	// Use default driver in duplex mode
	} else {
		JCALog("Open default driver \n");
		if (get_default_device(&driver->device_id) != noErr) {
			jack_error("Cannot open default device");
			goto error;
		}
		if (get_device_name_from_id(driver->device_id, driver->capture_driver_name) != noErr || get_device_name_from_id(driver->device_id, driver->playback_driver_name) != noErr) {
			jack_error("Cannot get device name from device ID");
			goto error;
		}
	}
	
	driver->client = client;
    driver->period_usecs =
		(((float) driver->frames_per_cycle) / driver->frame_rate) *
		1000000.0f;
	
	if (capturing) {
		err = get_total_channels(driver->device_id, &in_nChannels, true);
		if (err != noErr) { 
			jack_error("Cannot get input channel number");
			printError(err);
			goto error;
		} 
	}
	
	if (playing) {
		err = get_total_channels(driver->device_id, &out_nChannels, false);
		if (err != noErr) { 
			jack_error("Cannot get output channel number");
			printError(err);
			goto error;
		} 
	}
	
	if (inchannels > in_nChannels) {
        jack_error("This device hasn't required input channels inchannels = %ld in_nChannels = %ld", inchannels, in_nChannels);
		goto error;
    }
	
	if (outchannels > out_nChannels) {
        jack_error("This device hasn't required output channels outchannels = %ld out_nChannels = %ld", outchannels, out_nChannels);
		goto error;
    }

	if (inchannels == 0) {
		JCALog("Setup max in channels = %ld\n", in_nChannels);
		inchannels = in_nChannels; 
	}
		
	if (outchannels == 0) {
		JCALog("Setup max out channels = %ld\n", out_nChannels);
		outchannels = out_nChannels; 
	}

    // Setting buffer size
    outSize = sizeof(UInt32);
    err = AudioDeviceSetProperty(driver->device_id, NULL, 0, false, kAudioDevicePropertyBufferFrameSize, outSize, &nframes);
    if (err != noErr) {
        jack_error("Cannot set buffer size %ld", nframes);
        printError(err);
		goto error;
    }

	// Set sample rate
	outSize =  sizeof(Float64);
	err = AudioDeviceGetProperty(driver->device_id, 0, kAudioDeviceSectionGlobal, kAudioDevicePropertyNominalSampleRate, &outSize, &sampleRate);
	if (err != noErr) {
		jack_error("Cannot get current sample rate");
		printError(err);
		goto error;
	}

	if (samplerate != (jack_nframes_t)sampleRate) {
		sampleRate = (Float64)samplerate;
		
		// To get SR change notification
		err = AudioDeviceAddPropertyListener(driver->device_id, 0, true, kAudioDevicePropertyNominalSampleRate, sr_notification, driver);
		if (err != noErr) {
			jack_error("Error calling AudioDeviceAddPropertyListener with kAudioDevicePropertyNominalSampleRate");
			printError(err);
			return -1;
		}
		err = AudioDeviceSetProperty(driver->device_id, NULL, 0, kAudioDeviceSectionGlobal, kAudioDevicePropertyNominalSampleRate, outSize, &sampleRate);
		if (err != noErr) {
			jack_error("Cannot set sample rate = %ld", samplerate);
			printError(err);
			return -1;
		}
		
		// Waiting for SR change notification
		int count = 0;
		while (!driver->state && count++ < 100) {
			usleep(100000);
			JCALog("Wait count = %ld\n", count);
		}
		
		// Remove SR change notification
		AudioDeviceRemovePropertyListener(driver->device_id, 0, true, kAudioDevicePropertyNominalSampleRate, sr_notification);
	}

    // AUHAL
    ComponentDescription cd = {kAudioUnitType_Output, kAudioUnitSubType_HALOutput, kAudioUnitManufacturer_Apple, 0, 0};
    Component HALOutput = FindNextComponent(NULL, &cd);

    err1 = OpenAComponent(HALOutput, &driver->au_hal);
    if (err1 != noErr) {
		jack_error("Error calling OpenAComponent");
        printError(err1);
        goto error;
	}

    err1 = AudioUnitInitialize(driver->au_hal);
    if (err1 != noErr) {
		jack_error("Cannot initialize AUHAL unit");
		printError(err1);
        goto error;
	}

 	// Start I/O
	enableIO = 1;
	if (capturing && inchannels > 0) {
		JCALog("Setup AUHAL input\n");
        err1 = AudioUnitSetProperty(driver->au_hal, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &enableIO, sizeof(enableIO));
        if (err1 != noErr) {
            jack_error("Error calling AudioUnitSetProperty - kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input");
            printError(err1);
            goto error;
        }
    }
	
	if (playing && outchannels > 0) {
		JCALog("Setup AUHAL output\n");
		err1 = AudioUnitSetProperty(driver->au_hal, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &enableIO, sizeof(enableIO));
		if (err1 != noErr) {
			jack_error("Error calling AudioUnitSetProperty - kAudioOutputUnitProperty_EnableIO,kAudioUnitScope_Output");
			printError(err1);
			goto error;
		}
	}
	
	// Setup up choosen device, in both input and output cases
	err1 = AudioUnitSetProperty(driver->au_hal, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &driver->device_id, sizeof(AudioDeviceID));
	if (err1 != noErr) {
		jack_error("Error calling AudioUnitSetProperty - kAudioOutputUnitProperty_CurrentDevice");
		printError(err1);
		goto error;
	}

	// Set buffer size
	if (capturing && inchannels > 0) {
		err1 = AudioUnitSetProperty(driver->au_hal, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 1, (UInt32*)&nframes, sizeof(UInt32));
		if (err1 != noErr) {
			jack_error("Error calling AudioUnitSetProperty - kAudioUnitProperty_MaximumFramesPerSlice");
			printError(err1);
			goto error;
		}
	}
	
	if (playing && outchannels > 0) {
		err1 = AudioUnitSetProperty(driver->au_hal, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, (UInt32*)&nframes, sizeof(UInt32));
		if (err1 != noErr) {
			jack_error("Error calling AudioUnitSetProperty - kAudioUnitProperty_MaximumFramesPerSlice");
			printError(err1);
			goto error;
		}
	}

	// Setup channel map
	if (capturing && inchannels > 0 && inchannels < in_nChannels) {
        SInt32 chanArr[in_nChannels];
        for (i = 0; i < in_nChannels; i++) {
            chanArr[i] = -1;
        }
        for (i = 0; i < inchannels; i++) {
            chanArr[i] = i;
        }
        AudioUnitSetProperty(driver->au_hal, kAudioOutputUnitProperty_ChannelMap , kAudioUnitScope_Input, 1, chanArr, sizeof(SInt32) * in_nChannels);
        if (err1 != noErr) {
            jack_error("Error calling AudioUnitSetProperty - kAudioOutputUnitProperty_ChannelMap 1");
            printError(err1);
        }
    }

    if (playing && outchannels > 0 && outchannels < out_nChannels) {
        SInt32 chanArr[out_nChannels];
        for (i = 0;	i < out_nChannels; i++) {
            chanArr[i] = -1;
        }
        for (i = 0; i < outchannels; i++) {
            chanArr[i] = i;
        }
        err1 = AudioUnitSetProperty(driver->au_hal, kAudioOutputUnitProperty_ChannelMap, kAudioUnitScope_Output, 0, chanArr, sizeof(SInt32) * out_nChannels);
        if (err1 != noErr) {
            jack_error("Error calling AudioUnitSetProperty - kAudioOutputUnitProperty_ChannelMap 0");
            printError(err1);
        }
    }

	// Setup stream converters
  	srcFormat.mSampleRate = samplerate;
	srcFormat.mFormatID = kAudioFormatLinearPCM;
	srcFormat.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kLinearPCMFormatFlagIsNonInterleaved;
	srcFormat.mBytesPerPacket = sizeof(float);
	srcFormat.mFramesPerPacket = 1;
	srcFormat.mBytesPerFrame = sizeof(float);
	srcFormat.mChannelsPerFrame = outchannels;
	srcFormat.mBitsPerChannel = 32;

	err1 = AudioUnitSetProperty(driver->au_hal, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &srcFormat, sizeof(AudioStreamBasicDescription));
	if (err1 != noErr) {
		jack_error("Error calling AudioUnitSetProperty - kAudioUnitProperty_StreamFormat kAudioUnitScope_Input");
		printError(err1);
	}

	dstFormat.mSampleRate = samplerate;
	dstFormat.mFormatID = kAudioFormatLinearPCM;
	dstFormat.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kLinearPCMFormatFlagIsNonInterleaved;
	dstFormat.mBytesPerPacket = sizeof(float);
	dstFormat.mFramesPerPacket = 1;
	dstFormat.mBytesPerFrame = sizeof(float);
	dstFormat.mChannelsPerFrame = inchannels;
	dstFormat.mBitsPerChannel = 32;

	err1 = AudioUnitSetProperty(driver->au_hal, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &dstFormat, sizeof(AudioStreamBasicDescription));
	if (err1 != noErr) {
		jack_error("Error calling AudioUnitSetProperty - kAudioUnitProperty_StreamFormat kAudioUnitScope_Output");
		printError(err1);
	}

	// Setup callbacks
    if (inchannels > 0 && outchannels == 0) {
        AURenderCallbackStruct output;
        output.inputProc = render_input;
        output.inputProcRefCon = driver;
    	err1 = AudioUnitSetProperty(driver->au_hal, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &output, sizeof(output));
        if (err1 != noErr) {
            jack_error("Error calling  AudioUnitSetProperty - kAudioUnitProperty_SetRenderCallback 1");
            printError(err1);
            goto error;
        }
    } else {
        AURenderCallbackStruct output;
        output.inputProc = render;
        output.inputProcRefCon = driver;
        err1 = AudioUnitSetProperty(driver->au_hal, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &output, sizeof(output));
        if (err1 != noErr) {
            jack_error("Error calling AudioUnitSetProperty - kAudioUnitProperty_SetRenderCallback 0");
            printError(err1);
            goto error;
        }
    }

	if (capturing && inchannels > 0) {
		driver->input_list = (AudioBufferList*)malloc(sizeof(UInt32) + inchannels * sizeof(AudioBuffer));
		if (driver->input_list == 0)
			goto error;
		driver->input_list->mNumberBuffers = inchannels;
		
		// Prepare buffers
		for (i = 0; i < driver->capture_nchannels; i++) {
			driver->input_list->mBuffers[i].mNumberChannels = 1;
			driver->input_list->mBuffers[i].mDataByteSize = nframes * sizeof(float);
		}
	}

	err = AudioDeviceAddPropertyListener(driver->device_id, 0, true, kAudioDeviceProcessorOverload, notification, driver);
    if (err != noErr) {
		jack_error("Error calling AudioDeviceAddPropertyListener with kAudioDeviceProcessorOverload");
        goto error;
	}
		
	err = AudioDeviceAddPropertyListener(driver->device_id, 0, true, kAudioDevicePropertyNominalSampleRate, notification, driver);
    if (err != noErr) {
        jack_error("Error calling AudioDeviceAddPropertyListener with kAudioDevicePropertyNominalSampleRate");
        goto error;
    }
 
	driver->playback_nchannels = outchannels;
    driver->capture_nchannels = inchannels;
	return ((jack_driver_t *) driver);

  error:
	AudioUnitUninitialize(driver->au_hal);
	CloseComponent(driver->au_hal);
    jack_error("Cannot open the coreaudio driver");
    free(driver);
    return NULL;
}

/** free all memory allocated by a driver instance
*/
static void coreaudio_driver_delete(coreaudio_driver_t * driver)
{
 	AudioDeviceRemovePropertyListener(driver->device_id, 0, true, kAudioDeviceProcessorOverload, notification);
    free(driver->input_list);
 	AudioUnitUninitialize(driver->au_hal);
	CloseComponent(driver->au_hal);
    free(driver);
}

//== driver "plugin" interface =================================================

/* DRIVER "PLUGIN" INTERFACE */

const char driver_client_name[] = "coreaudio";

jack_driver_desc_t *driver_get_descriptor()
{
    jack_driver_desc_t *desc;
    unsigned int i;
    desc = calloc(1, sizeof(jack_driver_desc_t));

    strcpy(desc->name, "coreaudio");
    desc->nparams = 12;
    desc->params = calloc(desc->nparams, sizeof(jack_driver_param_desc_t));

    i = 0;
    strcpy(desc->params[i].name, "channels");
    desc->params[i].character = 'c';
    desc->params[i].type = JackDriverParamInt;
    desc->params[i].value.ui = 2;
    strcpy(desc->params[i].short_desc, "Maximum number of channels");
    strcpy(desc->params[i].long_desc, desc->params[i].short_desc);

    i++;
    strcpy(desc->params[i].name, "inchannels");
    desc->params[i].character = 'i';
    desc->params[i].type = JackDriverParamInt;
    desc->params[i].value.ui = 2;
    strcpy(desc->params[i].short_desc, "Maximum number of input channels");
    strcpy(desc->params[i].long_desc, desc->params[i].short_desc);

    i++;
    strcpy(desc->params[i].name, "outchannels");
    desc->params[i].character = 'o';
    desc->params[i].type = JackDriverParamInt;
    desc->params[i].value.ui = 2;
    strcpy(desc->params[i].short_desc, "Maximum number of output channels");
    strcpy(desc->params[i].long_desc, desc->params[i].short_desc);

    i++;
	strcpy(desc->params[i].name, "capture");
	desc->params[i].character = 'C';
	desc->params[i].type = JackDriverParamString;
	strcpy(desc->params[i].value.str, "will take default CoreAudio input device");
	strcpy(desc->params[i].short_desc, "Provide capture ports. Optionally set CoreAudio device name");
	strcpy(desc->params[i].long_desc, desc->params[i].short_desc);

	i++;
	strcpy(desc->params[i].name, "playback");
	desc->params[i].character = 'P';
	desc->params[i].type = JackDriverParamString;
	strcpy(desc->params[i].value.str, "will take default CoreAudio output device");
	strcpy(desc->params[i].short_desc, "Provide playback ports. Optionally set CoreAudio device name");
	strcpy(desc->params[i].long_desc, desc->params[i].short_desc);

    i++;
    strcpy(desc->params[i].name, "duplex");
    desc->params[i].character = 'D';
    desc->params[i].type = JackDriverParamBool;
    desc->params[i].value.i = TRUE;
    strcpy(desc->params[i].short_desc, "Capture and playback");
    strcpy(desc->params[i].long_desc, desc->params[i].short_desc);

    i++;
    strcpy(desc->params[i].name, "rate");
    desc->params[i].character = 'r';
    desc->params[i].type = JackDriverParamUInt;
    desc->params[i].value.ui = 44100U;
    strcpy(desc->params[i].short_desc, "Sample rate");
    strcpy(desc->params[i].long_desc, desc->params[i].short_desc);

    i++;
    strcpy(desc->params[i].name, "period");
    desc->params[i].character = 'p';
    desc->params[i].type = JackDriverParamUInt;
    desc->params[i].value.ui = 128U;
    strcpy(desc->params[i].short_desc, "Frames per period");
    strcpy(desc->params[i].long_desc, desc->params[i].short_desc);

    i++;
	strcpy(desc->params[i].name, "device");
	desc->params[i].character = 'd';
	desc->params[i].type = JackDriverParamString;
	desc->params[i].value.ui = 128U;
	strcpy(desc->params[i].value.str, "will take default CoreAudio device name");
	strcpy(desc->params[i].short_desc, "CoreAudio device name");
	strcpy(desc->params[i].long_desc, desc->params[i].short_desc);

 	i++;
	strcpy(desc->params[i].name, "input-latency");
	desc->params[i].character  = 'I';
	desc->params[i].type = JackDriverParamUInt;
	desc->params[i].value.i = 0;
	strcpy(desc->params[i].short_desc, "Extra input latency");
	strcpy(desc->params[i].long_desc, desc->params[i].short_desc);

	i++;
	strcpy(desc->params[i].name, "output-latency");
	desc->params[i].character  = 'O';
	desc->params[i].type = JackDriverParamUInt;
	desc->params[i].value.i  = 0;
	strcpy(desc->params[i].short_desc, "Extra output latency");
	strcpy(desc->params[i].long_desc, desc->params[i].short_desc);
	
	i++;
	strcpy(desc->params[i].name, "list-devices");
	desc->params[i].character  = 'l';
	desc->params[i].type = JackDriverParamBool;
	desc->params[i].value.i  = FALSE;
	strcpy(desc->params[i].short_desc, "Display available CoreAudio devices");
	strcpy(desc->params[i].long_desc, desc->params[i].short_desc);
	
    return desc;
}

jack_driver_t *driver_initialize(jack_client_t * client,
				 const JSList * params)
{
    jack_nframes_t srate = 44100; /* Some older Mac models only support this value */
    jack_nframes_t frames_per_interrupt = 128;
    int capture = FALSE;
    int playback = FALSE;
    int chan_in = 0;
    int chan_out = 0;
    char* capture_pcm_name = "";
	char* playback_pcm_name = "";
    const JSList *node;
    const jack_driver_param_t *param;
	jack_nframes_t systemic_input_latency = 0;
	jack_nframes_t systemic_output_latency = 0;

    for (node = params; node; node = jack_slist_next(node)) {
	param = (const jack_driver_param_t *) node->data;

	switch (param->character) {

		case 'd':
			capture_pcm_name = strdup(param->value.str);
			playback_pcm_name = strdup(param->value.str);
			break;

		case 'D':
			capture = TRUE;
			playback = TRUE;
			break;

		case 'c':
			chan_in = chan_out = (int) param->value.ui;
			break;

		case 'i':
			chan_in = (int) param->value.ui;
			break;

		case 'o':
			chan_out = (int) param->value.ui;
			break;

		case 'C':
			capture = TRUE;
			if (strcmp(param->value.str, "none") != 0) {
				capture_pcm_name = strdup(param->value.str);
			}
			break;

		case 'P':
			playback = TRUE;
			if (strcmp(param->value.str, "none") != 0) {
				playback_pcm_name = strdup(param->value.str);
			}
			break;

		case 'r':
			srate = param->value.ui;
			break;

		case 'p':
			frames_per_interrupt = (unsigned int) param->value.ui;
			break;

		case 'I':
			systemic_input_latency = param->value.ui;
			break;

		case 'O':
			systemic_output_latency = param->value.ui;
			break;
			
		case 'l':
			display_device_names();
			break;
		}
    }

    /* duplex is the default */
    if (!capture && !playback) {
		capture = TRUE;
		playback = TRUE;
    }
	
	return coreaudio_driver_new("coreaudio", client, frames_per_interrupt,
								srate, capture, playback, chan_in,
								chan_out, capture_pcm_name, playback_pcm_name, systemic_input_latency, systemic_output_latency);
}

void driver_finish(jack_driver_t * driver)
{
    coreaudio_driver_delete((coreaudio_driver_t *) driver);
}
