/*
 *  AudioRenderBridge.c
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

#include "AudioRenderBridge.h"
#include "AudioRender.h"

void *openPandaAudioInstance(float sampleRate, long bufferSize,
			     int inChannels, int outChannels, char *device)
{
    AudioRender *newInst =
	new AudioRender(sampleRate, bufferSize, inChannels, outChannels,
			device);
    if (newInst->status)
		return newInst;
    return NULL;
}

void closePandaAudioInstance(void *instance)
{
    if (instance) {
		AudioRender *inst = (AudioRender *) instance;
		inst->StopAudio();
		delete inst;
    }
}

int startPandaAudioProcess(void *instance)
{
    if (instance) {
		AudioRender *inst = (AudioRender *) instance;
		return inst->StartAudio();
    }
    return FALSE;
}

int stopPandaAudioProcess(void *instance)
{
    if (instance) {
		AudioRender *inst = (AudioRender *) instance;
		return inst->StopAudio();
    }
    return FALSE;
}

float **getPandaAudioInputs(void *instance)
{
    if (instance) {
		AudioRender *inst = (AudioRender *) instance;
		return inst->inBuffers;
    }
    return NULL;
}

float **getPandaAudioOutputs(void *instance)
{
    if (instance) {
		AudioRender *inst = (AudioRender *) instance;
		return inst->outBuffers;
    }
    return NULL;
}

void *getHostData(void *instance)
{
    if (instance) {
		AudioRender *inst = (AudioRender *) instance;
		return inst->jackData;
    }
    return NULL;
}

void setHostData(void *instance, void *hostData)
{
    if (instance) {
		AudioRender *inst = (AudioRender *) instance;
		inst->jackData = hostData;
    }
}

void setCycleFun(void *instance, JackRunCyclePtr fun)
{
    if (instance) {
		AudioRender *inst = (AudioRender *) instance;
		inst->f_JackRunCycle = fun;
    }
}

void setParameter(void *instance, int id, void *data)
{
    if (instance) {
		AudioRender *inst = (AudioRender *) instance;
		switch (id) {
		case 'inte':
			inst->isInterleaved = (int *) data;
			break;
		case 'nstr':
			inst->numberOfStreams = (int *) data;
			*inst->numberOfStreams = inst->n_streams;
			break;
		case 'cstr':
			inst->channelsPerStream = (int *) data;
			break;
		case 'nstO':
			inst->out_numberOfStreams = (int *) data;
			*inst->out_numberOfStreams = inst->n_out_streams;
			break;
		case 'cstO':
			inst->out_channelsPerStream = (int *) data;
			break;
		}
    }
}
