/*
 *  AudioRender.cpp
 *  Under Artistic License.
 *  This code is part of Panda framework (moduleloader.cpp)
 *  http://xpanda.sourceforge.net
 *
 *  Created by Johnny Petrantoni on Fri Jan 30 2004.
 *  Copyright (c) 2004 Johnny Petrantoni. All rights reserved.
 *
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

extern "C" void JCALog(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "JCA: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void PrintStreamDesc(AudioStreamBasicDescription * inDesc)
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
    AudioRender::gOutputChannels = vChannels;
    AudioRender::theRender = this;
    isProcessing = false;

    if (status) {
	inBuffers = (float **) malloc(sizeof(float *) * vInChannels);
	outBuffers = (float **) malloc(sizeof(float *) * vChannels);
	JCALog("AudioRender created.\n");
	JCALog("Standard driver.\n");
    } else
	JCALog("error while creating AudioRender.\n");
}

AudioRender::~AudioRender()
{
    if (status) {
	if (isProcessing)
	    AudioDeviceStop(vDevice, process);
	OSStatus err = AudioDeviceRemoveIOProc(vDevice, process);
	if (err == noErr)
	    status = false;
	free(inBuffers);
	free(outBuffers);
    }
}

bool AudioRender::ConfigureAudioProc(float sampleRate, long bufferSize,
				     int channels, int inChannels,
				     char *device)
{

    OSStatus err;
    UInt32 size;
    Boolean isWritable;

    JCALog("Wanted DEVICE: %s\n", device);

    err =
	AudioHardwareGetPropertyInfo(kAudioHardwarePropertyDevices, &size,
				     &isWritable);
    if (err != noErr)
	return false;

    int manyDevices = size / sizeof(AudioDeviceID);

    AudioDeviceID devices[manyDevices];
    err =
	AudioHardwareGetProperty(kAudioHardwarePropertyDevices, &size,
				 &devices);
    if (err != noErr)
	return false;

    bool found = false;

    for (int i = 0; i < manyDevices; i++) {
	size = sizeof(char) * 256;
	char name[256];
	err =
	    AudioDeviceGetProperty(devices[i], 0, false,
				   kAudioDevicePropertyDeviceName, &size,
				   &name);
	JCALog("Read DEVICE: %s\n", name);
	if (err != noErr)
	    return false;
	if (strncmp(device, name, strlen(device)) == 0) {	// steph : name seems to be limited to 32 character, thus compare the common part only 
	    JCALog("Found DEVICE: %s %ld\n", name, device);
	    vDevice = devices[i];
	    found = true;
	}
    }

    if (!found) {
	JCALog("Cannot find device \"%s\".\n", device);
	return false;
    }

    char deviceName[256];
    err =
	AudioDeviceGetProperty(vDevice, 0, false,
			       kAudioDevicePropertyDeviceName, &size,
			       &deviceName);
    if (err != noErr)
	return false;

    JCALog("DEVICE: %s.\n", deviceName);

    size = sizeof(AudioStreamBasicDescription);
    AudioStreamBasicDescription SR;
    err =
	AudioDeviceGetProperty(vDevice, 0, false,
			       kAudioDevicePropertyStreamFormat, &size,
			       &SR);
    if (err != noErr)
	return false;

    err =
	AudioDeviceGetPropertyInfo(vDevice, 0, false,
				   kAudioDevicePropertyStreams, &size,
				   &isWritable);
    if (err != noErr)
	return false;

    vChannels =
	(int) SR.mChannelsPerFrame * (size / sizeof(AudioStreamID));

    n_out_streams = size / sizeof(AudioStreamID);

    if (channels > vChannels) {
	JCALog("cannot find requested output channels\n");
	return false;
    }

    if (vChannels >= channels)
	vChannels = channels;

    JCALog("OUTPUT CHANNELS: %d.\n", vChannels);

    err =
	AudioDeviceGetPropertyInfo(vDevice, 0, true,
				   kAudioDevicePropertyStreamFormat, &size,
				   &isWritable);
    if (err != noErr) {
	vInChannels = 0;
	goto endInChan;
    }

    size = sizeof(AudioStreamBasicDescription);
    AudioStreamBasicDescription inSR;
    err =
	AudioDeviceGetProperty(vDevice, 0, true,
			       kAudioDevicePropertyStreamFormat, &size,
			       &inSR);
    if (err != noErr)
	return false;

    err =
	AudioDeviceGetPropertyInfo(vDevice, 0, true,
				   kAudioDevicePropertyStreams, &size,
				   &isWritable);
    if (err != noErr)
	return false;

    vInChannels =
	(int) inSR.mChannelsPerFrame * (size / sizeof(AudioStreamID));

    n_streams = size / sizeof(AudioStreamID);

  endInChan:

    if (inChannels > vInChannels) {
	JCALog("cannot find requested input channels\n");
	return false;
    }

    if (vInChannels >= inChannels)
	vInChannels = inChannels;

    JCALog("INPUT CHANNELS: %d.\n", vInChannels);

    UInt32 bufFrame;
    size = sizeof(UInt32);
    err =
	AudioDeviceGetProperty(vDevice, 0, false,
			       kAudioDevicePropertyBufferFrameSize, &size,
			       &bufFrame);
    if (err != noErr)
	return false;

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
	    if (err != noErr)
		return false;
	    vBufferSize = (long) newBufFrame;
	}
    }

    JCALog("BUFFER SIZE: %ld.\n", vBufferSize);

    vSampleRate = (float) SR.mSampleRate;

    if ((float) SR.mSampleRate != sampleRate) {
	JCALog("I'm trying to set a new sample rate.\n");
	UInt32 theSize = sizeof(AudioStreamBasicDescription);
	SR.mSampleRate = (Float64) sampleRate;
	err =
	    AudioDeviceSetProperty(vDevice, NULL, 0, false,
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
	    if (err != noErr)
		return false;
	    vSampleRate = (float) newCheckSR.mSampleRate;
	}
    }

    JCALog("SAMPLE RATE: %f.\n", vSampleRate);

    PrintStreamDesc(&SR);

    err = AudioDeviceAddIOProc(vDevice, process, this);
    if (err != noErr)
	return false;

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
	    if (channel == classe->vInChannels)
		break;
	}
	channel = 0;
	for (unsigned int a = 0; a < outOutputData->mNumberBuffers; a++) {
	    classe->outBuffers[channel] =
		(float *) outOutputData->mBuffers[a].mData;
	    channel++;
	    if (channel == classe->vChannels)
		break;
	}
    } else {
	for (unsigned int b = 0; b < inInputData->mNumberBuffers; b++) {
	    classe->channelsPerStream[b] =
		(int) inInputData->mBuffers[b].mNumberChannels;
	    classe->inBuffers[b] = (float *) inInputData->mBuffers[b].mData;	// but jack will read only the inBuffers[0], anyway that should not be a problem.
	}
	for (unsigned int b = 0; b < outOutputData->mNumberBuffers; b++) {
	    classe->out_channelsPerStream[b] =
		(int) outOutputData->mBuffers[b].mNumberChannels;
	    classe->outBuffers[b] = (float *) outOutputData->mBuffers[b].mData;	// but jack will read only the outBuffers[0], anyway that should not be a problem.
	}
    }

    classe->f_JackRunCycle(classe->jackData, classe->vBufferSize);

    return noErr;
}

float **AudioRender::getADC()
{
    if (AudioRender::theRender == NULL)
	return NULL;
    return AudioRender::theRender->inBuffers;
}

float **AudioRender::getDAC()
{
    if (AudioRender::theRender == NULL)
	return NULL;
    return AudioRender::theRender->outBuffers;
}
