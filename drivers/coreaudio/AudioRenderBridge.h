/*
 *  AudioRenderBridge.h
 *  jack_coreaudio
 *  Under Artistic License.
 *
 *  Created by Johnny Petrantoni on Fri Jan 30 2004.
 *  Copyright (c) 2004 Johnny Petrantoni. All rights reserved.
 *
 */

typedef int (*JackRunCyclePtr) (void *driver, long bufferSize);

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
    void setParameter(void *instance, int id, void *data);

#ifdef __cplusplus
}
#endif
