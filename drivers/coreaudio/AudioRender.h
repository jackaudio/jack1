/*
 *  AudioRender.h
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

#include <Carbon/Carbon.h>
#include <CoreAudio/CoreAudio.h>

#define	Print4CharCode(msg, c)	{														\
	UInt32 __4CC_number = (c);		\
		char __4CC_string[5];		\
	memcpy(__4CC_string, &__4CC_number, 4);	\
	__4CC_string[4] = 0;			\
	printf("%s'%s'\n", (msg), __4CC_string);\
}

typedef int (*JackRunCyclePtr) (void *driver, long bufferSize);

class AudioRender {
  public:
    AudioRender(float sampleRate, long bufferSize, int inChannels,
		int outChannels, char *device);
    ~AudioRender();
    bool ConfigureAudioProc(float sampleRate, long bufferSize,
			    int channels, int inChannels, char *device);
    bool StartAudio();
    bool StopAudio();
    void *jackData;
    bool status;
    JackRunCyclePtr f_JackRunCycle;
    float **inBuffers;
    float **outBuffers;
    AudioDeviceID vDevice;
    float vSampleRate;
    long vBufferSize;
    int vChannels;		//output channels
    int vInChannels;		//input chennels
    static float **getADC();
    static float **getDAC();
    static float gSampleRate;
    static long gBufferSize;
    static int gInputChannels;
    static int gOutputChannels;
    static bool isProcessing;
    static const AudioTimeStamp *gTime;
    int *isInterleaved, *numberOfStreams, *channelsPerStream,
	*out_numberOfStreams, *out_channelsPerStream;
    int n_streams, n_out_streams;
  private:
    static OSStatus process(AudioDeviceID inDevice,
			    const AudioTimeStamp * inNow,
			    const AudioBufferList * inInputData,
			    const AudioTimeStamp * inInputTime,
			    AudioBufferList * outOutputData,
			    const AudioTimeStamp * inOutputTime,
			    void *inClientData);
    static AudioRender *theRender;
};
