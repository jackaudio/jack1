/*
 *  AudioRender.cpp
 *
 *  Copyright (c) 2004 Johnny Petrantoni. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to the Free
 *  Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 *  Created by Johnny Petrantoni on Fri Jan 30 2004.
 *  This code is part of Panda framework (moduleloader.cpp)
 *  http://xpanda.sourceforge.net
 */

#include <unistd.h>
#include "AudioRender.h"

float AudioRender::gSampleRate = 0.0;
long AudioRender::gBufferSize = 0;
int AudioRender::gInputChannels = 0;
int AudioRender::gOutputChannels = 0;
AudioRender *AudioRender::theRender = NULL;
bool AudioRender::isProcessing = false;
const AudioTimeStamp *AudioRender::gTime;

#define PRINTDEBUG 1

extern "C" void JCALog(char *fmt, ...)
{
#ifdef PRINTDEBUG
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "JCA: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
#endif
}

static OSStatus GetTotalChannels(AudioDeviceID device, UInt32  *channelCount, Boolean isInput) 
{
    OSStatus err = noErr;
    UInt32 outSize;
    Boolean outWritable;
    AudioBufferList *bufferList = NULL;
    unsigned short i;

    *channelCount = 0;
    err = AudioDeviceGetPropertyInfo(device, 0, isInput, kAudioDevicePropertyStreamConfiguration,  &outSize, &outWritable);
    if (err == noErr)
    {
		JCALog("AudioDeviceGetPropertyInfo kAudioDevicePropertyStreamConfiguration: OK\n");
        bufferList = (AudioBufferList*)malloc(outSize);
        
        err = AudioDeviceGetProperty(device, 0, isInput, kAudioDevicePropertyStreamConfiguration, &outSize, bufferList);
        if (err == noErr) {                                                               
            for (i = 0; i < bufferList->mNumberBuffers; i++) 
                *channelCount += bufferList->mBuffers[i].mNumberChannels;
        }
        free(bufferList);
    }
 
    return (err);
}

static void PrintStreamDesc(AudioStreamBasicDescription * inDesc)
{
    if (!inDesc) {
		JCALog("Can't print a NULL desc!\n");
		return;
    }

    JCALog("- - - - - - - - - - - - - - - - - - - -\n");
    JCALog("  Sample Rate:%f\n", inDesc->mSampleRate);
    JCALog("  Format ID:%.*s\n", (int) sizeof(inDesc->mFormatID),
	   (char *) &inDesc->mFormatID);
    JCALog("  Format Flags:%lX\n", inDesc->mFormatFlags);
    JCALog("  Bytes per Packet:%ld\n", inDesc->mBytesPerPacket);
    JCALog("  Frames per Packet:%ld\n", inDesc->mFramesPerPacket);
    JCALog("  Bytes per Frame:%ld\n", inDesc->mBytesPerFrame);
    JCALog("  Channels per Frame:%ld\n", inDesc->mChannelsPerFrame);
    JCALog("  Bits per Channel:%ld\n", inDesc->mBitsPerChannel);
    JCALog("- - - - - - - - - - - - - - - - - - - -\n");
}

static void printError(OSStatus err) 
{
#ifdef PRINTDEBUG
	switch (err) {
		case kAudioHardwareNoError:
			JCALog("error code : kAudioHardwareNoError\n");
			break;
		 case kAudioHardwareNotRunningError:
			JCALog("error code : kAudioHardwareNotRunningError\n");
			break;
		case kAudioHardwareUnspecifiedError:
			printf("error code : kAudioHardwareUnspecifiedError\n");
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


AudioRender::AudioRender(float sampleRate, long bufferSize, int inChannels,
						int outChannels,
						char *device):vSampleRate(sampleRate),
vBufferSize(bufferSize)
{
    inBuffers = NULL;
    outBuffers = NULL;
    status =
		ConfigureAudioProc(sampleRate, bufferSize, outChannels, inChannels,
			   device);

    AudioRender::gSampleRate = vSampleRate;
    AudioRender::gBufferSize = vBufferSize;
    AudioRender::gInputChannels = vInChannels;
    AudioRender::gOutputChannels = vOutChannels;
    AudioRender::theRender = this;
    isProcessing = false;

    if (status) {
		inBuffers = (float **) malloc(sizeof(float *) * vInChannels);
		outBuffers = (float **) malloc(sizeof(float *) * vOutChannels);
		JCALog("AudioRender created.\n");
		JCALog("Standard driver.\n");
    } else
		JCALog("error while creating AudioRender.\n");
}

AudioRender::~AudioRender()
{
    if (status) {
		if (isProcessing) AudioDeviceStop(vDevice, process);
		AudioDeviceRemoveIOProc(vDevice, process);
		AudioDeviceRemovePropertyListener(vDevice,0,true,kAudioDeviceProcessorOverload,notification);
		free(inBuffers);
		free(outBuffers);
    }
}

bool AudioRender::ConfigureAudioProc(float sampleRate, long bufferSize,
									int outChannels, int inChannels,
									char *device)
{

    OSStatus err;
    UInt32 size;
    Boolean isWritable;
	AudioStreamBasicDescription SR;

    JCALog("Wanted DEVICE: %s\n", device);

    err = AudioHardwareGetPropertyInfo(kAudioHardwarePropertyDevices, &size,
										&isWritable);
    if (err != noErr) return false;

    int manyDevices = size / sizeof(AudioDeviceID);

    AudioDeviceID devices[manyDevices];
    err = AudioHardwareGetProperty(kAudioHardwarePropertyDevices, &size,
									&devices);
    if (err != noErr) return false;

    bool found = false;

    for (int i = 0; i < manyDevices; i++) {
		size = sizeof(char) * 256;
		char name[256];
		err = AudioDeviceGetProperty(devices[i], 0, false,
									kAudioDevicePropertyDeviceName, &size,
									&name);
		JCALog("Read DEVICE: %s\n", name);
		if (err != noErr) return false;
		if (strncmp(device, name, strlen(device)) == 0) {	// steph : name seems to be limited to 32 character, thus compare the common part only 
			JCALog("Found DEVICE: %s %ld\n", name, devices[i]);
			vDevice = devices[i];
			found = true;
		}
    }

    if (!found) {
		JCALog("Cannot find device \"%s\".\n", device);
		return false;
    }

    char deviceName[256];
    err = AudioDeviceGetProperty(vDevice, 0, false,
								kAudioDevicePropertyDeviceName, &size,
								&deviceName);
    if (err != noErr) return false;
    JCALog("DEVICE: %s.\n", deviceName);
	
	JCALog("WANTED OUTPUT CHANNELS: %d.\n", outChannels);
	err = AudioDeviceGetPropertyInfo(vDevice, 0, false,
									kAudioDevicePropertyStreamFormat, &size,
									&isWritable);
    if (err != noErr) {
		vOutChannels = 0;
	}else{
		size = sizeof(AudioStreamBasicDescription);
		err = AudioDeviceGetProperty(vDevice, 0, false,
									kAudioDevicePropertyStreamFormat, &size,
									&SR);
		if (err != noErr) {
			JCALog("AudioDeviceGetPropertyInfo kAudioDevicePropertyStreamFormat error: %ld\n",err);
			printError(err);
			return false;
		}
		JCALog("AudioDeviceGetPropertyInfo kAudioDevicePropertyStreamFormat: OK\n");

		err = AudioDeviceGetPropertyInfo(vDevice, 0, false,
										kAudioDevicePropertyStreams, &size,
										&isWritable);
		if (err != noErr) {
			JCALog("AudioDeviceGetPropertyInfo kAudioDevicePropertyStreams error: %ld\n",err);
			printError(err);
			return false;
		}
		JCALog("AudioDeviceGetPropertyInfo kAudioDevicePropertyStreams: OK\n");

		err = GetTotalChannels(vDevice,(UInt32*)&vOutChannels,false);
		if (err != noErr) return false;
		
		n_out_streams = size / sizeof(AudioStreamID);
	}	
	
	if (outChannels > vOutChannels) {
		JCALog("cannot find requested output channels\n");
		return false;
	}

	if (vOutChannels >= outChannels)
		vOutChannels = outChannels;

	JCALog("OUTPUT CHANNELS: %d.\n", vOutChannels);
	
	JCALog("WANTED INPUT CHANNELS: %d.\n", inChannels);
	err = AudioDeviceGetPropertyInfo(vDevice, 0, true,
									kAudioDevicePropertyStreamFormat, &size,
									&isWritable);
			
    if (err != noErr) {
		vInChannels = 0;
	}else{

		size = sizeof(AudioStreamBasicDescription);
		err = AudioDeviceGetProperty(vDevice, 0, true,
									kAudioDevicePropertyStreamFormat, &size,
									&SR);
		if (err != noErr) {
			JCALog("AudioDeviceGetPropertyInfo kAudioDevicePropertyStreamForma terror: %ld\n",err);
			printError(err);
			return false;
		}
		JCALog("AudioDeviceGetPropertyInfo kAudioDevicePropertyStreamFormat: OK\n");
		
		err = AudioDeviceGetPropertyInfo(vDevice, 0, true,
										kAudioDevicePropertyStreams, &size,
										&isWritable);
		if (err != noErr) {
			JCALog("AudioDeviceGetPropertyInfo kAudioDevicePropertyStreams error: %ld\n",err);
			printError(err);
			return false;
		}
		JCALog("AudioDeviceGetPropertyInfo kAudioDevicePropertyStreams: OK\n");
		
		err = GetTotalChannels(vDevice,(UInt32*)&vInChannels,true);
		if (err != noErr) return false;

		n_in_streams = size / sizeof(AudioStreamID);
	}

    if (inChannels > vInChannels) {
		JCALog("cannot find requested input channels\n");
		return false;
    }

    if (vInChannels >= inChannels)
		vInChannels = inChannels;

    JCALog("INPUT CHANNELS: %d.\n", vInChannels);

    UInt32 bufFrame;
    size = sizeof(UInt32);
    err = AudioDeviceGetProperty(vDevice, 0, false,
			       kAudioDevicePropertyBufferFrameSize, &size,
			       &bufFrame);
    if (err != noErr) return false;
	
	JCALog("Internal buffer size %d.\n", bufFrame);

    vBufferSize = (long) bufFrame;

    if ((long) bufFrame != bufferSize) {
		JCALog("I'm trying to set a new buffer size.\n");
		UInt32 theSize = sizeof(UInt32);
		UInt32 newBufferSize = (UInt32) bufferSize;
		err =
	    AudioDeviceSetProperty(vDevice, NULL, 0, false,
				   kAudioDevicePropertyBufferFrameSize,
				   theSize, &newBufferSize);
		if (err != noErr) {
			JCALog("Cannot set a new buffer size.\n");
			return false;
		} else {
			UInt32 newBufFrame;
			size = sizeof(UInt32);
			err =
			AudioDeviceGetProperty(vDevice, 0, false,
						   kAudioDevicePropertyBufferFrameSize,
						   &size, &newBufFrame);
			if (err != noErr) return false;
			vBufferSize = (long) newBufFrame;
		}
    }

    JCALog("BUFFER SIZE: %ld.\n", vBufferSize);

    vSampleRate = (float) SR.mSampleRate;

    if ((float) SR.mSampleRate != sampleRate) {
		JCALog("I'm trying to set a new sample rate.\n");
		UInt32 theSize = sizeof(AudioStreamBasicDescription);
		SR.mSampleRate = (Float64) sampleRate;
		err = AudioDeviceSetProperty(vDevice, NULL, 0, false,
					   kAudioDevicePropertyStreamFormat,
					   theSize, &SR);
		if (err != noErr) {
			JCALog("Cannot set a new sample rate.\n");
			return false;
		} else {
			size = sizeof(AudioStreamBasicDescription);
			AudioStreamBasicDescription newCheckSR;
			err =
			AudioDeviceGetProperty(vDevice, 0, false,
						   kAudioDevicePropertyStreamFormat,
						   &size, &newCheckSR);
			if (err != noErr) return false;
			vSampleRate = (float) newCheckSR.mSampleRate;
		}
    }

    JCALog("SAMPLE RATE: %f.\n", vSampleRate);

    PrintStreamDesc(&SR);

    err = AudioDeviceAddIOProc(vDevice, process, this);
    if (err != noErr) return false;
	
	err = AudioDeviceAddPropertyListener(vDevice,0,true,kAudioDeviceProcessorOverload,notification,this);
	if (err != noErr) return false;

    return true;
}

bool AudioRender::StartAudio()
{
    if (status) {
		OSStatus err = AudioDeviceStart(vDevice, process);
		if (err != noErr)
			return false;
		AudioRender::isProcessing = true;
		return true;
    }
    return false;
}

bool AudioRender::StopAudio()
{
    if (status) {
		OSStatus err = AudioDeviceStop(vDevice, process);
		if (err != noErr)
			return false;
		AudioRender::isProcessing = false;
		return true;
    }
    return false;
}

OSStatus AudioRender::notification(AudioDeviceID inDevice,
									UInt32 inChannel,
									Boolean	isInput,
									AudioDevicePropertyID inPropertyID,
									void* inClientData)
{
	AudioRender *classe = (AudioRender *) inClientData;
	
	switch(inPropertyID) {
	
		case kAudioDeviceProcessorOverload:
			JCALog("notification kAudioDeviceProcessorOverload\n");
			classe->f_JackXRun(classe->jackData, 100);
			break;
	}
	
	return noErr;
}

OSStatus AudioRender::process(AudioDeviceID inDevice,
			      const AudioTimeStamp * inNow,
			      const AudioBufferList * inInputData,
			      const AudioTimeStamp * inInputTime,
			      AudioBufferList * outOutputData,
			      const AudioTimeStamp * inOutputTime,
			      void *inClientData)
{
    AudioRender *classe = (AudioRender *) inClientData;
    int channel = 0;

    AudioRender::gTime = inInputTime;

    *classe->isInterleaved = (inInputData->mNumberBuffers != 0
			      && inInputData->mBuffers[0].
			      mNumberChannels == 1) ? FALSE : TRUE;

	if (!*classe->isInterleaved) {
		for (unsigned int a = 0; a < inInputData->mNumberBuffers; a++) {
			classe->inBuffers[channel] =
				(float *) inInputData->mBuffers[a].mData;
			channel++;
			if (channel == classe->vInChannels) break;
		}
		channel = 0;
		for (unsigned int a = 0; a < outOutputData->mNumberBuffers; a++) {
			classe->outBuffers[channel] =
				(float *) outOutputData->mBuffers[a].mData;
			channel++;
			if (channel == classe->vOutChannels) break;
		}
	} else {
		for (unsigned int b = 0; b < inInputData->mNumberBuffers; b++) {
			classe->channelsPerInputStream[b] =
				(int) inInputData->mBuffers[b].mNumberChannels;
			classe->inBuffers[b] = (float *) inInputData->mBuffers[b].mData;	// but jack will read only the inBuffers[0], anyway that should not be a problem.
		}
		for (unsigned int b = 0; b < outOutputData->mNumberBuffers; b++) {
			classe->channelsPerOutputStream[b] =
				(int) outOutputData->mBuffers[b].mNumberChannels;
			classe->outBuffers[b] = (float *) outOutputData->mBuffers[b].mData;	// but jack will read only the outBuffers[0], anyway that should not be a problem.
		}
    }

    classe->f_JackRunCycle(classe->jackData, classe->vBufferSize);
    return noErr;
}

float **AudioRender::getADC()
{
    return (AudioRender::theRender == NULL) ? NULL : AudioRender::theRender->inBuffers;
}

float **AudioRender::getDAC()
{
    return (AudioRender::theRender == NULL) ? NULL : AudioRender::theRender->outBuffers;		
}
