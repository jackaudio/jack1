/*
 *  AudioRenderBridge.h
 *  jack_coreaudio
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

typedef int (*JackRunCyclePtr) (void *driver, long bufferSize);
typedef void (*JackXRunPtr) (void* driver, float delayed_usecs);
 
#ifdef __cplusplus
extern "C" {
#endif

    void *openPandaAudioInstance(float sampleRate, long bufferSize,
				 int inChannels, int outChannels,
				 char *device);
    void closePandaAudioInstance(void *instance);
    int startPandaAudioProcess(void *instance);
    int stopPandaAudioProcess(void *instance);
    float **getPandaAudioInputs(void *instance);
    float **getPandaAudioOutputs(void *instance);
    void *getHostData(void *instance);
    void setHostData(void *instance, void *hostData);
    void setCycleFun(void *instance, JackRunCyclePtr fun);
	void setXRunFun(void *instance, JackXRunPtr fun);
    void setParameter(void *instance, int id, void *data);

#ifdef __cplusplus
}
#endif
