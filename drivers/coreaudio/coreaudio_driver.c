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
 
 
 TODO:
	- fix cpu load behavior.
	- multiple-device processing.
 */

#include <stdio.h>
#include <string.h>
#include <jack/engine.h>
#include <CoreAudio/CoreAudio.h>

#include "coreaudio_driver.h"
#include "AudioRenderBridge.h"

const int CAVersion = 2;

void JCALog(char *fmt, ...);

static OSStatus GetDeviceNameFromID(AudioDeviceID id, char name[60])
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

    theStatus = AudioHardwareGetPropertyInfo(kAudioHardwarePropertyDevices,
				     &theSize, NULL);
    nDevices = theSize / sizeof(AudioDeviceID);
    theDeviceList =
	(AudioDeviceID *) malloc(nDevices * sizeof(AudioDeviceID));
    theStatus = AudioHardwareGetProperty(kAudioHardwarePropertyDevices, 
					&theSize, theDeviceList);

    *id = theDeviceList[i];
    return theStatus;
}

int coreaudio_runCycle(void *driver, long bufferSize)
{
    coreaudio_driver_t *ca_driver = (coreaudio_driver_t *) driver;
    ca_driver->last_wait_ust = jack_get_microseconds();
    return ca_driver->engine->run_cycle(ca_driver->engine, bufferSize, 0);
}

static int
coreaudio_driver_attach(coreaudio_driver_t * driver,
			jack_engine_t * engine)
{
    jack_port_t *port;
    int port_flags;
    channel_t chn;
    char buf[JACK_PORT_NAME_SIZE];

    driver->engine = engine;

    driver->engine->set_buffer_size(engine, driver->frames_per_cycle);
    driver->engine->set_sample_rate(engine, driver->frame_rate);

    port_flags =
	JackPortIsOutput | JackPortIsPhysical | JackPortIsTerminal;

    /*
       if (driver->has_hw_monitoring) {
       port_flags |= JackPortCanMonitor;
       }
     */

    for (chn = 0; chn < driver->capture_nchannels; chn++) {
		//snprintf (buf, sizeof(buf) - 1, "capture_%lu", chn+1);
		snprintf(buf, sizeof(buf) - 1, "%s:out%lu", driver->driver_name,
			 chn + 1);

		if ((port = jack_port_register(driver->client, buf,
					JACK_DEFAULT_AUDIO_TYPE, port_flags,
					0)) == NULL) {
			jack_error("coreaudio: cannot register port for %s", buf);
			break;
		}

		/* XXX fix this so that it can handle: systemic (external) latency
		 */

		jack_port_set_latency(port, driver->frames_per_cycle);
		driver->capture_ports =
			jack_slist_append(driver->capture_ports, port);
    }

    port_flags = JackPortIsInput | JackPortIsPhysical | JackPortIsTerminal;

	for (chn = 0; chn < driver->playback_nchannels; chn++) {
		//snprintf (buf, sizeof(buf) - 1, "playback_%lu", chn+1);
		snprintf(buf, sizeof(buf) - 1, "%s:in%lu", driver->driver_name,
			 chn + 1);

		if ((port = jack_port_register(driver->client, buf,
					JACK_DEFAULT_AUDIO_TYPE, port_flags,
					0)) == NULL) {
			jack_error("coreaudio: cannot register port for %s", buf);
			break;
		}

		/* XXX fix this so that it can handle: systemic (external) latency
		 */

		jack_port_set_latency(port, driver->frames_per_cycle);
		driver->playback_ports =
			jack_slist_append(driver->playback_ports, port);
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
    int i;

    if (!driver->isInterleaved) {
		for (i = 0; i < driver->playback_nchannels; i++) {
			memset(driver->outcoreaudio[i], 0x0, nframes * sizeof(float));
		}
    } else {
		memset(driver->outcoreaudio[0], 0x0,
		nframes * sizeof(float) * driver->playback_nchannels);
    }

    return 0;
}

static int
coreaudio_driver_read(coreaudio_driver_t * driver, jack_nframes_t nframes)
{
    jack_default_audio_sample_t *buf;
    channel_t chn;
    jack_port_t *port;
    JSList *node;
    int i;

    int b = 0;

    for (chn = 0, node = driver->capture_ports; node;
		node = jack_slist_next(node), chn++) {

		port = (jack_port_t *) node->data;

		if (!driver->isInterleaved) {
			if (jack_port_connected(port)
			&& (driver->incoreaudio[chn] != NULL)) {
				float *in = driver->incoreaudio[chn];
				buf = jack_port_get_buffer(port, nframes);
				memcpy(buf, in, sizeof(float) * nframes);
			}
		} else {
			if (jack_port_connected(port)
			&& (driver->incoreaudio[b] != NULL)) {
				int channels = driver->channelsPerInputStream[b];
				if (channels <= chn) {
					b++;
					if (driver->numberOfInputStreams > 1
						&& b < driver->numberOfInputStreams) {
						channels = driver->channelsPerInputStream[b];
						chn = 0;
					} else
						return 0;
				}
				if (channels > 0) {
					float *in = driver->incoreaudio[b];
					buf = jack_port_get_buffer(port, nframes);
					for (i = 0; i < nframes; i++)
					buf[i] = in[channels * i + chn];
				}
			}
		}
	}

    driver->engine->transport_cycle_start(driver->engine,
					  jack_get_microseconds());
    return 0;
}

static int
coreaudio_driver_write(coreaudio_driver_t * driver, jack_nframes_t nframes)
{
    jack_default_audio_sample_t *buf;
    channel_t chn;
    jack_port_t *port;
    JSList *node;
    int i,bytes = nframes*sizeof(float);

    int b = 0;

    for (chn = 0, node = driver->playback_ports; node;
		node = jack_slist_next(node), chn++) {

		port = (jack_port_t *) node->data;

		if (!driver->isInterleaved) {
			if (jack_port_connected(port)
			&& (driver->outcoreaudio[chn] != NULL)) {
				float *out = driver->outcoreaudio[chn];
				buf = jack_port_get_buffer(port, nframes);
				memcpy(out, buf, sizeof(float) * nframes);
				/* clear to avoid playing dirty buffers when the client does not produce output anymore */
				memset(buf, 0, bytes);
			}
		} else {
			if (jack_port_connected(port)
				&& (driver->outcoreaudio[b] != NULL)) {
				int channels = driver->channelsPerOuputStream[b];
				if (channels <= chn) {
					b++;
					if (driver->numberOfOuputStreams > 1
						&& b < driver->numberOfOuputStreams) {
						channels = driver->channelsPerOuputStream[b];
						chn = 0;
					} else
						return 0;
				}
				if (channels > 0) {
					float *out = driver->outcoreaudio[b];
					buf = jack_port_get_buffer(port, nframes);
					for (i = 0; i < nframes; i++)
					out[channels * i + chn] = buf[i];
					/* clear to avoid playing dirty buffers when the client does not produce output anymore */
					memset(buf, 0, bytes);
				}
			}
		}
	}

    return 0;
}

static int coreaudio_driver_audio_start(coreaudio_driver_t * driver)
{
    return (!startPandaAudioProcess(driver->stream)) ? -1 : 0;
}

static int coreaudio_driver_audio_stop(coreaudio_driver_t * driver)
{
    return (!stopPandaAudioProcess(driver->stream)) ? -1 : 0;
}

#if 0
static int
coreaudio_driver_bufsize(coreaudio_driver_t * driver,
			 jack_nframes_t nframes)
{

    /* This gets called from the engine server thread, so it must
     * be serialized with the driver thread.  Stopping the audio
     * also stops that thread. */

    closePandaAudioInstance(driver->stream);

    driver->stream =
		openPandaAudioInstance((float) driver->frame_rate,
			       driver->frames_per_cycle, driver->capturing,
			       driver->playing, &driver->driver_name[0]);

    if (!driver->stream)
		return FALSE;

    setHostData(driver->stream, driver);
    setCycleFun(driver->stream, coreaudio_runCycle);
    setParameter(driver->stream, 'inte', &driver->isInterleaved);

    driver->incoreaudio = getPandaAudioInputs(driver->stream);
    driver->outcoreaudio = getPandaAudioOutputs(driver->stream);

    return startPandaAudioProcess(driver->stream);
}
#endif

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
					   AudioDeviceID deviceID)
{
    coreaudio_driver_t *driver;

    JCALog("coreaudio beta %d driver\n", CAVersion);

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

    driver->attach = (JackDriverAttachFunction) coreaudio_driver_attach;
    driver->detach = (JackDriverDetachFunction) coreaudio_driver_detach;
    driver->read = (JackDriverReadFunction) coreaudio_driver_read;
    driver->write = (JackDriverReadFunction) coreaudio_driver_write;
    driver->null_cycle =
	(JackDriverNullCycleFunction) coreaudio_driver_null_cycle;
    //driver->bufsize = (JackDriverBufSizeFunction) coreaudio_driver_bufsize;
    driver->start = (JackDriverStartFunction) coreaudio_driver_audio_start;
    driver->stop = (JackDriverStopFunction) coreaudio_driver_audio_stop;
    driver->stream = NULL;

    char deviceName[60];
    bzero(&deviceName[0], sizeof(char) * 60);
	get_device_id_from_num(0,&deviceID);

    if (!driver_name) {
		if (GetDeviceNameFromID(deviceID, deviceName) != noErr)
			goto error;
    } else {
		strcpy(&deviceName[0], driver_name);
    }

    driver->stream =
		openPandaAudioInstance((float) rate, frames_per_cycle, chan_in,
			       chan_out, &deviceName[0]);

    if (!driver->stream)
		goto error;

    driver->client = client;
    driver->period_usecs =
		(((float) driver->frames_per_cycle) / driver->frame_rate) *
		1000000.0f;

    setHostData(driver->stream, driver);
    setCycleFun(driver->stream, coreaudio_runCycle);
    setParameter(driver->stream, 'inte', &driver->isInterleaved);
    setParameter(driver->stream, 'nstr', &driver->numberOfInputStreams);
    setParameter(driver->stream, 'nstO', &driver->numberOfOuputStreams);

    JCALog("There are %d input streams.\n", driver->numberOfInputStreams);
    JCALog("There are %d output streams.\n", driver->numberOfOuputStreams);

    driver->channelsPerInputStream =
		(int *) malloc(sizeof(int) * driver->numberOfInputStreams);
    driver->channelsPerOuputStream =
		(int *) malloc(sizeof(int) * driver->numberOfOuputStreams);

    setParameter(driver->stream, 'cstr', driver->channelsPerInputStream);
    setParameter(driver->stream, 'cstO', driver->channelsPerOuputStream);

    driver->incoreaudio = getPandaAudioInputs(driver->stream);
    driver->outcoreaudio = getPandaAudioOutputs(driver->stream);

    driver->playback_nchannels = chan_out;
    driver->capture_nchannels = chan_in;

    strcpy(&driver->driver_name[0], &deviceName[0]);
	// steph
    //jack_init_time();
    return ((jack_driver_t *) driver);

  error:

    JCALog("Cannot open the coreaudio stream\n");
    free(driver);
    return NULL;
}

/** free all memory allocated by a driver instance
*/
static void coreaudio_driver_delete(coreaudio_driver_t * driver)
{
    /* Close coreaudio stream and terminate */
    closePandaAudioInstance(driver->stream);
    free(driver->channelsPerInputStream);
    free(driver->channelsPerOuputStream);
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
    desc->nparams = 10;
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
		}
    }

    /* duplex is the default */

    if (!capture && !playback) {
		capture = TRUE;
		playback = TRUE;
    }

    return coreaudio_driver_new("coreaudio", client, frames_per_interrupt,
				srate, capture, playback, chan_in,
				chan_out, name, deviceID);
}

void driver_finish(jack_driver_t * driver)
{
    coreaudio_driver_delete((coreaudio_driver_t *) driver);
}
