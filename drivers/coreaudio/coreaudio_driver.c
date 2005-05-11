/*
 Copyright © Grame, 2003.
 Copyright © Johnny Petrantoni, 2003.
 
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
 
 TODO:
	- fix cpu load behavior.
	- multiple-device processing.
 */

#include <stdio.h>
#include <string.h>
#include <jack/engine.h>
#include "coreaudio_driver.h"

const int CAVersion = 3;

#define PRINTDEBUG 1

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

static void printError1(OSStatus err)
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
            JCALog("error code : unknown\n");
            break;
    }
#endif
}

static OSStatus get_device_name_from_id(AudioDeviceID id, char name[60])
{
    UInt32 size = sizeof(char) * 60;
    OSStatus res = AudioDeviceGetProperty(id, 0, false,
					   kAudioDevicePropertyDeviceName,
					   &size,
					   &name[0]);
    return res;
}

static OSStatus get_device_id_from_num(int i, AudioDeviceID * id)
{
    OSStatus theStatus;
    UInt32 theSize;
    int nDevices;
    AudioDeviceID* theDeviceList;

    theStatus = AudioHardwareGetPropertyInfo(kAudioHardwarePropertyDevices,
				     &theSize, NULL);
    nDevices = theSize / sizeof(AudioDeviceID);
    theDeviceList =
	(AudioDeviceID*) malloc(nDevices * sizeof(AudioDeviceID));
    theStatus = AudioHardwareGetProperty(kAudioHardwarePropertyDevices, 
					&theSize, theDeviceList);

    *id = theDeviceList[i];
    return theStatus;
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
    }else{
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
    }else{
		ca_driver->last_wait_ust = jack_get_microseconds();
		ca_driver->engine->transport_cycle_start(ca_driver->engine,
					  jack_get_microseconds());
		return ca_driver->engine->run_cycle(ca_driver->engine, inNumberFrames, 0);
	}
}

OSStatus notification(AudioDeviceID inDevice,
        UInt32 inChannel,
        Boolean	isInput,
        AudioDevicePropertyID inPropertyID,
        void* inClientData)
{
    coreaudio_driver_t* ca_driver = (coreaudio_driver_t*)inClientData;
    if (inPropertyID == kAudioDeviceProcessorOverload) {
		ca_driver->xrun_detected = 1;
    }
    return noErr;
}

static int
coreaudio_driver_attach(coreaudio_driver_t * driver,
			jack_engine_t * engine)
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

    driver->engine->set_buffer_size(engine, driver->frames_per_cycle);
    driver->engine->set_sample_rate(engine, driver->frame_rate);

    port_flags = JackPortIsOutput | JackPortIsPhysical | JackPortIsTerminal;

    /*
       if (driver->has_hw_monitoring) {
			port_flags |= JackPortCanMonitor;
       }
	*/

    for (chn = 0; chn < driver->capture_nchannels; chn++) {
		//snprintf (buf, sizeof(buf) - 1, "capture_%lu", chn+1);
		
		err = AudioDeviceGetPropertyInfo(driver->device_id, chn + 1, true, kAudioDevicePropertyChannelName, &size, &isWritable);
		if (err == noErr && size > 0)  {
			err = AudioDeviceGetProperty(driver->device_id, chn + 1, true, kAudioDevicePropertyChannelName, &size, channel_name);	
			if (err != noErr) 
				JCALog("AudioDeviceGetProperty kAudioDevicePropertyChannelName error \n");
			snprintf(buf, sizeof(buf) - 1, "%s:out_%s%lu", driver->driver_name, channel_name, chn + 1);
		} else {
			snprintf(buf, sizeof(buf) - 1, "%s:out%lu", driver->driver_name, chn + 1);
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
		//snprintf (buf, sizeof(buf) - 1, "playback_%lu", chn+1);
		
		err = AudioDeviceGetPropertyInfo(driver->device_id, chn + 1, false, kAudioDevicePropertyChannelName, &size, &isWritable);
		if (err == noErr && size > 0)  {
			err = AudioDeviceGetProperty(driver->device_id, chn + 1, false, kAudioDevicePropertyChannelName, &size, channel_name);	
			if (err != noErr) 
				JCALog("AudioDeviceGetProperty kAudioDevicePropertyChannelName error \n");
			snprintf(buf, sizeof(buf) - 1, "%s:in_%s%lu", driver->driver_name, channel_name, chn + 1);
		} else {
			snprintf(buf, sizeof(buf) - 1, "%s:in%lu", driver->driver_name, chn + 1);
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
coreaudio_driver_detach(coreaudio_driver_t * driver,
			jack_engine_t * engine)
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
coreaudio_driver_null_cycle(coreaudio_driver_t * driver,
			    jack_nframes_t nframes)
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
static jack_driver_t *coreaudio_driver_new(char *name,
					   jack_client_t * client,
					   jack_nframes_t frames_per_cycle,
					   jack_nframes_t rate,
					   int capturing,
					   int playing,
					   int chan_in,
					   int chan_out,
					   char *driver_name,
					   AudioDeviceID deviceID,
					   jack_nframes_t capture_latency, 
					   jack_nframes_t playback_latency)
{
    coreaudio_driver_t *driver;
	OSStatus err = noErr;
	ComponentResult err1;
    UInt32 outSize;
    Boolean isWritable;
	AudioStreamBasicDescription srcFormat, dstFormat, sampleRate;
	int in_nChannels, out_nChannels, i;

    driver = (coreaudio_driver_t *) calloc(1, sizeof(coreaudio_driver_t));
    jack_driver_init((jack_driver_t *) driver);

    if (!jack_power_of_two(frames_per_cycle)) {
		fprintf(stderr, "CA: -p must be a power of two.\n");
		goto error;
    }

    driver->frames_per_cycle = frames_per_cycle;
    driver->frame_rate = rate;
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

    if (!driver_name) {
		if (get_device_name_from_id(deviceID, driver->driver_name) != noErr)
			goto error;
    } else {
		JCALog("Use driver name from command line \n");
		strcpy(driver->driver_name, driver_name);
    }

	driver->client = client;
    driver->period_usecs =
		(((float) driver->frames_per_cycle) / driver->frame_rate) *
		1000000.0f;

    driver->playback_nchannels = chan_out;
    driver->capture_nchannels = chan_in;
	driver->device_id = deviceID;
	
	// Setting buffer size
    outSize = sizeof(UInt32);
    err = AudioDeviceSetProperty(driver->device_id, NULL, 0, false, 
		kAudioDevicePropertyBufferFrameSize, outSize, &frames_per_cycle);
    if (err != noErr) {
        jack_error("Cannot set buffer size %ld\n", frames_per_cycle);
        printError1(err);
        return NULL;
    }

    // Setting sample rate
    outSize = sizeof(AudioStreamBasicDescription);
    err = AudioDeviceGetProperty(driver->device_id, 0, false, 
		kAudioDevicePropertyStreamFormat, &outSize, &sampleRate);
    if (err != noErr) {
        jack_error("Cannot get sample rate\n");
        printError1(err);
        return NULL;
    }

    if (rate != (unsigned long)sampleRate.mSampleRate) {
        sampleRate.mSampleRate = (Float64)rate;
        err = AudioDeviceSetProperty(driver->device_id, NULL, 0, 
			false, kAudioDevicePropertyStreamFormat, outSize, &sampleRate);
        if (err != noErr) {
            jack_error("Cannot set sample rate %ld\n", rate);
            printError1(err);
            return NULL;
        }
    }

	// AUHAL
	ComponentDescription cd = {kAudioUnitType_Output, 
		kAudioUnitSubType_HALOutput, kAudioUnitManufacturer_Apple, 0, 0};
	Component HALOutput = FindNextComponent(NULL, &cd);	
	
	err1 = OpenAComponent(HALOutput, &driver->au_hal);
	if (err1 != noErr)
        goto error;

	err1 = AudioUnitSetProperty(driver->au_hal, kAudioOutputUnitProperty_CurrentDevice, 
		kAudioUnitScope_Global, 0, &driver->device_id, sizeof(AudioDeviceID));
	if (err1 != noErr) { 
		JCALog("error: calling AudioUnitSetProperty - kAudioOutputUnitProperty_CurrentDevice\n"); 
		return NULL; 
	}
	
	err1 = AudioUnitInitialize(driver->au_hal); 
	if (err1 != noErr)
        goto error;
	
	outSize = 1;
	err1 = AudioUnitSetProperty(driver->au_hal, kAudioOutputUnitProperty_EnableIO,
		kAudioUnitScope_Output, 0, &outSize, sizeof(outSize));
	if (err1 != noErr) { 
		JCALog("error: calling AudioUnitSetProperty - kAudioOutputUnitProperty_EnableIO,kAudioUnitScope_Output\n"); 
		goto error; 
	}
	
	if (chan_in > 0) {
		outSize = 1;
		err1 = AudioUnitSetProperty(driver->au_hal, kAudioOutputUnitProperty_EnableIO, 
			kAudioUnitScope_Input, 1, &outSize, sizeof(outSize));
		if (err1 != noErr) { 
			JCALog("error: calling AudioUnitSetProperty - kAudioOutputUnitProperty_EnableIO,kAudioUnitScope_Input\n"); 
			goto error; 
		}
	}
	
	err1 = AudioUnitSetProperty(driver->au_hal, kAudioUnitProperty_MaximumFramesPerSlice, 
		kAudioUnitScope_Global, 0, (UInt32*)&frames_per_cycle, sizeof(UInt32));
	if (err1 != noErr) { 
		JCALog("error: calling AudioUnitSetProperty - kAudioUnitProperty_MaximumFramesPerSlice\n"); 
		goto error; 
	}

	err1 = AudioUnitGetPropertyInfo(driver->au_hal, kAudioOutputUnitProperty_ChannelMap, 
		kAudioUnitScope_Input, 1, &outSize, &isWritable);
	if (err1 != noErr) 
		JCALog("error: calling AudioUnitSetProperty - kAudioOutputUnitProperty_ChannelMap-INFO 1\n");
	
	in_nChannels = outSize / sizeof(SInt32);
	
	err1 = AudioUnitGetPropertyInfo(driver->au_hal, kAudioOutputUnitProperty_ChannelMap, 
		kAudioUnitScope_Output, 0, &outSize, &isWritable);
	if (err1 != noErr) 
		JCALog("error: calling AudioUnitSetProperty - kAudioOutputUnitProperty_ChannelMap-INFO 0\n");

	out_nChannels = outSize / sizeof(SInt32);
	
	if (chan_out > out_nChannels) {
		JCALog("This device hasn't required output channels.\n"); 
		goto error; 
	}
	if (chan_in > in_nChannels) { 
		JCALog("This device hasn't required input channels.\n"); 
		goto error; 
	}

	if (chan_out < out_nChannels) {
		SInt32 chanArr[out_nChannels]; // ???
		for (i = 0;i < out_nChannels; i++) {
			chanArr[i] = -1;
		}
		for (i = 0; i < chan_out; i++) {
			chanArr[i] = i;
		}
		err1 = AudioUnitSetProperty(driver->au_hal, kAudioOutputUnitProperty_ChannelMap, 
			kAudioUnitScope_Output, 0, chanArr, sizeof(SInt32) * out_nChannels);
		if (err1 != noErr) 
			JCALog("error: calling AudioUnitSetProperty - kAudioOutputUnitProperty_ChannelMap 0\n");
	}
	
	if (chan_in < in_nChannels) {
		SInt32 chanArr[in_nChannels];
		for (i = 0; i < in_nChannels; i++) {
			chanArr[i] = -1;
		}
		for(i = 0;i < chan_in; i++) {
			chanArr[i] = i;
		}
		AudioUnitSetProperty(driver->au_hal, kAudioOutputUnitProperty_ChannelMap, 
			kAudioUnitScope_Input, 1, chanArr, sizeof(SInt32) * in_nChannels);
		if (err1 != noErr) 
			JCALog("error: calling AudioUnitSetProperty - kAudioOutputUnitProperty_ChannelMap 1\n");
	}

	srcFormat.mSampleRate = rate;
    srcFormat.mFormatID = kAudioFormatLinearPCM;
    srcFormat.mFormatFlags = kLinearPCMFormatFlagIsBigEndian |
							kLinearPCMFormatFlagIsNonInterleaved | 
							kLinearPCMFormatFlagIsPacked | 
							kLinearPCMFormatFlagIsFloat;
    srcFormat.mBytesPerPacket = sizeof(float);
    srcFormat.mFramesPerPacket = 1;
    srcFormat.mBytesPerFrame = sizeof(float);
    srcFormat.mChannelsPerFrame = chan_out;
    srcFormat.mBitsPerChannel = 32;

	err1 = AudioUnitSetProperty(driver->au_hal, kAudioUnitProperty_StreamFormat, 
		kAudioUnitScope_Input, 0, &srcFormat, sizeof(AudioStreamBasicDescription));
	if (err1 != noErr) 
		JCALog("error: calling AudioUnitSetProperty - kAudioUnitProperty_StreamFormat kAudioUnitScope_Input\n");
		
	dstFormat.mSampleRate = rate;
    dstFormat.mFormatID = kAudioFormatLinearPCM;
    dstFormat.mFormatFlags = kLinearPCMFormatFlagIsBigEndian |
							kLinearPCMFormatFlagIsNonInterleaved | 
							kLinearPCMFormatFlagIsPacked | 
							kLinearPCMFormatFlagIsFloat;
    dstFormat.mBytesPerPacket = sizeof(float);
    dstFormat.mFramesPerPacket = 1;
    dstFormat.mBytesPerFrame = sizeof(float);
    dstFormat.mChannelsPerFrame = chan_in;
    dstFormat.mBitsPerChannel = 32;
	
	err1 = AudioUnitSetProperty(driver->au_hal, kAudioUnitProperty_StreamFormat, 
		kAudioUnitScope_Output, 1, &dstFormat, sizeof(AudioStreamBasicDescription));
	if (err1 != noErr) 
		JCALog("error: calling AudioUnitSetProperty - kAudioUnitProperty_StreamFormat kAudioUnitScope_Output\n");
	
	if (chan_in > 0 && chan_out== 0) {
		AURenderCallbackStruct output;
		output.inputProc = render_input;
		output.inputProcRefCon = driver;
		err1 = AudioUnitSetProperty(driver->au_hal, kAudioUnitProperty_SetRenderCallback, 
			kAudioUnitScope_Output, 1, &output, sizeof(output));
		if (err1 != noErr) { 
			JCALog("AudioUnitSetProperty - kAudioUnitProperty_SetRenderCallback 1\n"); 
			goto error; 
		}
	} else {
		AURenderCallbackStruct output;
		output.inputProc = render;
		output.inputProcRefCon = driver;
		err1 = AudioUnitSetProperty(driver->au_hal, kAudioUnitProperty_SetRenderCallback, 
			kAudioUnitScope_Input, 0, &output, sizeof(output));
		if (err1 != noErr) { 
			JCALog("AudioUnitSetProperty - kAudioUnitProperty_SetRenderCallback 0\n"); 
			goto error; 
		}
	}
	
    driver->input_list = (AudioBufferList*)malloc(sizeof(UInt32) + chan_in * sizeof(AudioBuffer));
    if (driver->input_list == 0)
        goto error;
    driver->input_list->mNumberBuffers = chan_in;

    driver->output_list = (AudioBufferList*)malloc(sizeof(UInt32) + chan_out * sizeof(AudioBuffer));
    if (driver->output_list == 0)
        goto error;
    driver->output_list->mNumberBuffers = chan_out;
	
	err = AudioDeviceAddPropertyListener(driver->device_id, 0, true, kAudioDeviceProcessorOverload, notification, driver);
    if (err != noErr)
        goto error;
 
	// Prepare buffers
    for (i = 0; i < driver->capture_nchannels; i++) {
        driver->input_list->mBuffers[i].mNumberChannels = 1;
        driver->input_list->mBuffers[i].mDataByteSize = frames_per_cycle * sizeof(float);
    }

    for (i = 0; i < driver->playback_nchannels; i++) {
        driver->output_list->mBuffers[i].mNumberChannels = 1;
        driver->output_list->mBuffers[i].mDataByteSize = frames_per_cycle * sizeof(float);
    }
	
	return ((jack_driver_t *) driver);

  error:
	AudioUnitUninitialize(driver->au_hal);
	CloseComponent(driver->au_hal);
    jack_error("Cannot open the coreaudio driver\n");
    free(driver);
    return NULL;
}

/** free all memory allocated by a driver instance
*/
static void coreaudio_driver_delete(coreaudio_driver_t * driver)
{
 	AudioDeviceRemovePropertyListener(driver->device_id, 0, true, kAudioDeviceProcessorOverload, notification);
    free(driver->input_list);
    free(driver->output_list);
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
    strcpy(desc->params[i].name, "channel");
    desc->params[i].character = 'c';
    desc->params[i].type = JackDriverParamInt;
    desc->params[i].value.ui = 2;
    strcpy(desc->params[i].short_desc, "Maximum number of channels");
    strcpy(desc->params[i].long_desc, desc->params[i].short_desc);

    i++;
    strcpy(desc->params[i].name, "channelin");
    desc->params[i].character = 'i';
    desc->params[i].type = JackDriverParamInt;
    desc->params[i].value.ui = 2;
    strcpy(desc->params[i].short_desc, "Maximum number of input channels");
    strcpy(desc->params[i].long_desc, desc->params[i].short_desc);

    i++;
    strcpy(desc->params[i].name, "channelout");
    desc->params[i].character = 'o';
    desc->params[i].type = JackDriverParamInt;
    desc->params[i].value.ui = 2;
    strcpy(desc->params[i].short_desc, "Maximum number of ouput channels");
    strcpy(desc->params[i].long_desc, desc->params[i].short_desc);

    i++;
    strcpy(desc->params[i].name, "capture");
    desc->params[i].character = 'C';
    desc->params[i].type = JackDriverParamBool;
    desc->params[i].value.i = TRUE;
    strcpy(desc->params[i].short_desc, "Whether or not to capture");
    strcpy(desc->params[i].long_desc, desc->params[i].short_desc);

    i++;
    strcpy(desc->params[i].name, "playback");
    desc->params[i].character = 'P';
    desc->params[i].type = JackDriverParamBool;
    desc->params[i].value.i = TRUE;
    strcpy(desc->params[i].short_desc, "Whether or not to playback");
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
    strcpy(desc->params[i].name, "name");
    desc->params[i].character = 'n';
    desc->params[i].type = JackDriverParamString;
    desc->params[i].value.ui = 128U;
    strcpy(desc->params[i].short_desc, "Driver name");
    strcpy(desc->params[i].long_desc, desc->params[i].short_desc);

    i++;
    strcpy(desc->params[i].name, "id");
    desc->params[i].character = 'I';
    desc->params[i].type = JackDriverParamInt;
    desc->params[i].value.i = 0;
    strcpy(desc->params[i].short_desc, "Audio Device ID");
    strcpy(desc->params[i].long_desc, desc->params[i].short_desc);
	
	i++;
	strcpy(desc->params[i].name, "input-latency");
	desc->params[i].character  = 'l';
	desc->params[i].type = JackDriverParamUInt;
	desc->params[i].value.i = 0;
	strcpy(desc->params[i].short_desc, "Extra input latency");
	strcpy(desc->params[i].long_desc, desc->params[i].short_desc);

	i++;
	strcpy(desc->params[i].name, "output-latency");
	desc->params[i].character  = 'L';
	desc->params[i].type = JackDriverParamUInt;
	desc->params[i].value.i  = 0;
	strcpy(desc->params[i].short_desc, "Extra output latency");
	strcpy(desc->params[i].long_desc, desc->params[i].short_desc);
	
    return desc;
}

jack_driver_t *driver_initialize(jack_client_t * client,
				 const JSList * params)
{
    jack_nframes_t srate = 44100;
    jack_nframes_t frames_per_interrupt = 128;
    int capture = FALSE;
    int playback = FALSE;
    int chan_in = 2;
    int chan_out = 2;
    char *name = NULL;
    AudioDeviceID deviceID = 0;
    const JSList *node;
    const jack_driver_param_t *param;
	jack_nframes_t systemic_input_latency = 0;
	jack_nframes_t systemic_output_latency = 0;
	
	get_device_id_from_num(0,&deviceID); // takes a default value (first device)

    for (node = params; node; node = jack_slist_next(node)) {
	param = (const jack_driver_param_t *) node->data;

	switch (param->character) {

		case 'n':
			name = (char *) param->value.str;
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

		case 'I':
			deviceID = (AudioDeviceID) param->value.ui;
			break;
			
		case 'l':
			systemic_input_latency = param->value.ui;
			break;

		case 'L':
			systemic_output_latency = param->value.ui;
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
				chan_out, name, deviceID, systemic_input_latency, systemic_output_latency);
}

void driver_finish(jack_driver_t * driver)
{
    coreaudio_driver_delete((coreaudio_driver_t *) driver);
}
