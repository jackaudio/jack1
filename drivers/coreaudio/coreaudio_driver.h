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
    
	30-01-04, Johnny Petrantoni: first code of the coreaudio driver.
   
*/

#ifndef __jack_coreaudio_driver_h__
#define __jack_coreaudio_driver_h__

#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioConverter.h>
#include <AudioUnit/AudioUnit.h>

#include <jack/types.h>
#include <jack/hardware.h>
#include <jack/driver.h>
#include <jack/jack.h>
#include <jack/internal.h>

typedef struct {

    JACK_DRIVER_DECL struct _jack_engine *engine;

    jack_nframes_t frame_rate;
    jack_nframes_t frames_per_cycle;
    unsigned long user_nperiods;
    int capturing;
    int playing;

    channel_t playback_nchannels;
    channel_t capture_nchannels;

    jack_client_t *client;
    JSList *capture_ports;
    JSList *playback_ports;

	char driver_name[256];
 	
	AudioUnit au_hal;
	AudioBufferList* input_list;
	AudioBufferList* output_list;
	AudioDeviceID device_id;
	
	int xrun_detected;
	int null_cycle_occured;

} coreaudio_driver_t;

#define kVersion 01

#endif /* __jack_coreaudio_driver_h__ */
