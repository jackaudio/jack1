/*
 *  AudioRenderBridge.c
 *  jack_coreaudio
 *  Under Artistic License.
 *
 *  Created by Johnny Petrantoni on Fri Jan 30 2004.
 *  Copyright (c) 2004 Johnny Petrantoni. All rights reserved.
 *
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
