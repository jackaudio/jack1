/*
    Copyright (C) 2001 Paul Davis 

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

    $Id$
*/

#ifndef __jack_alsa_driver_h__
#define __jack_alsa_driver_h__

#include <alsa/asoundlib.h>

#include <jack/types.h>
#include <jack/hardware.h>
#include <jack/driver.h>
#include <jack/memops.h>
#include <jack/jack.h>

typedef void (*ReadCopyFunction)  (jack_default_audio_sample_t *dst, char *src,
				   unsigned long src_bytes,
				   unsigned long src_skip_bytes);
typedef void (*WriteCopyFunction) (char *dst, jack_default_audio_sample_t *src,
				   unsigned long src_bytes,
				   unsigned long dst_skip_bytes,
				   dither_state_t *state);
typedef void (*CopyCopyFunction)  (char *dst, char *src,
				   unsigned long src_bytes,
				   unsigned long dst_skip_bytes,
				   unsigned long src_skip_byte);

typedef struct {

    JACK_DRIVER_DECL

    unsigned long long            poll_last;
    unsigned long long            poll_next;
    char                        **playback_addr;
    char                        **capture_addr;
    const snd_pcm_channel_area_t *capture_areas;
    const snd_pcm_channel_area_t *playback_areas;
    struct pollfd                *pfd;
    unsigned int                  playback_nfds;
    unsigned int                  capture_nfds;
    unsigned long                 interleave_unit;
    unsigned long                 capture_interleave_skip;
    unsigned long                 playback_interleave_skip;
    channel_t                     max_nchannels;
    channel_t                     user_nchannels;
    channel_t                     playback_nchannels;
    channel_t                     capture_nchannels;
    unsigned long                 sample_bytes;

    jack_nframes_t                     frame_rate;
    jack_nframes_t                     frames_per_cycle;
    float                         cpu_mhz;
    jack_nframes_t                     capture_frame_latency;
    jack_nframes_t                     playback_frame_latency;

    unsigned long                *silent;
    char                         *alsa_name;
    char                         *alsa_driver;
    snd_pcm_uframes_t             buffer_frames;
    unsigned long                 channels_not_done;
    unsigned long                 channel_done_bits;
    snd_pcm_format_t              sample_format;
    float                         max_sample_val;
    unsigned long                 user_nperiods;
    unsigned long                 nfragments;
    unsigned long                 last_mask;
    snd_ctl_t                    *ctl_handle;
    snd_pcm_t                    *playback_handle;
    snd_pcm_t                    *capture_handle;
    snd_pcm_hw_params_t          *playback_hw_params;
    snd_pcm_sw_params_t          *playback_sw_params;
    snd_pcm_hw_params_t          *capture_hw_params;
    snd_pcm_sw_params_t          *capture_sw_params;
    jack_hardware_t              *hw;  
    ClockSyncStatus              *clock_sync_data;
    struct _jack_engine          *engine;
    jack_client_t                *client;
    JSList                       *capture_ports;
    JSList                       *playback_ports;

    unsigned long input_monitor_mask;

    char   soft_mode : 1;
    char   hw_monitoring : 1;
    char   hw_metering : 1;
    char   all_monitor_in : 1;
    char   capture_and_playback_not_synced : 1;
    char   interleaved : 1;

    ReadCopyFunction read_via_copy;
    WriteCopyFunction write_via_copy;
    CopyCopyFunction channel_copy;

    int             dither;
    dither_state_t *dither_state;

    SampleClockMode clock_mode;
    JSList *clock_sync_listeners;
    pthread_mutex_t clock_sync_lock;
    unsigned long next_clock_sync_listener_id;
    char has_clock_sync_reporting : 1;
    char has_hw_monitoring : 1;
    char has_hw_metering : 1;
} alsa_driver_t;

static __inline__ void alsa_driver_mark_channel_done (alsa_driver_t *driver, channel_t chn) {
	driver->channels_not_done &= ~(1<<chn);
	driver->silent[chn] = 0;
}

static __inline__ void alsa_driver_silence_on_channel (alsa_driver_t *driver, channel_t chn, jack_nframes_t nframes) {
	if (driver->interleaved) {
		memset_interleave 
			(driver->playback_addr[chn],
			 0, nframes * driver->sample_bytes,
			 driver->interleave_unit,
			 driver->playback_interleave_skip);
	} else {
		memset (driver->playback_addr[chn], 0, nframes * driver->sample_bytes);
	}
	alsa_driver_mark_channel_done (driver,chn);
}

static __inline__ void alsa_driver_silence_on_channel_no_mark (alsa_driver_t *driver, channel_t chn, jack_nframes_t nframes) {
	if (driver->interleaved) {
		memset_interleave 
			(driver->playback_addr[chn],
			 0, nframes * driver->sample_bytes,
			 driver->interleave_unit,
			 driver->playback_interleave_skip);
	} else {
		memset (driver->playback_addr[chn], 0, nframes * driver->sample_bytes);
	}
}

static __inline__ void alsa_driver_read_from_channel (alsa_driver_t *driver, 
					   channel_t channel, jack_default_audio_sample_t *buf, 
					   jack_nframes_t nsamples)
{
	driver->read_via_copy (buf, 
			       driver->capture_addr[channel],
			       nsamples, 
			       driver->capture_interleave_skip);
}

static __inline__ void alsa_driver_write_to_channel (alsa_driver_t *driver,
				   channel_t channel, 
				   jack_default_audio_sample_t *buf, 
				   jack_nframes_t nsamples)
{
	driver->write_via_copy (driver->playback_addr[channel],
				buf, 
				nsamples, 
				driver->playback_interleave_skip,
				driver->dither_state+channel);
	alsa_driver_mark_channel_done (driver, channel);
}

static __inline__ void alsa_driver_copy_channel (alsa_driver_t *driver, 
			       channel_t input_channel, 
			       channel_t output_channel,
			       jack_nframes_t nsamples) {

	driver->channel_copy (driver->playback_addr[output_channel],
			      driver->capture_addr[input_channel],
			      nsamples * driver->sample_bytes,
			      driver->playback_interleave_skip,
			      driver->capture_interleave_skip);
	alsa_driver_mark_channel_done (driver, output_channel);
}

void  alsa_driver_set_clock_sync_status (alsa_driver_t *driver, channel_t chn, ClockSyncStatus status);
int   alsa_driver_listen_for_clock_sync_status (alsa_driver_t *, ClockSyncListenerFunction, void *arg);
int   alsa_driver_stop_listen_for_clock_sync_status (alsa_driver_t *, unsigned int);
void  alsa_driver_clock_sync_notify (alsa_driver_t *, channel_t chn, ClockSyncStatus);


#endif /* __jack_alsa_driver_h__ */
