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

#include <math.h>
#include <stdio.h>
#include <memory.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <getopt.h>

#include <jack/alsa_driver.h>
#include <jack/types.h>
#include <jack/internal.h>
#include <jack/engine.h>
#include <jack/hammerfall.h>
#include <jack/hdsp.h>
#include <jack/ice1712.h>
#include <jack/generic.h>
#include <jack/time.h>

extern void store_work_time (int);
extern void store_wait_time (int);
extern void show_wait_times ();
extern void show_work_times ();

#undef DEBUG_WAKEUP

static void
alsa_driver_release_channel_dependent_memory (alsa_driver_t *driver)

{
	if (driver->playback_addr) {
		free (driver->playback_addr);
		driver->playback_addr = 0;
	}

	if (driver->capture_addr) {
		free (driver->capture_addr);
		driver->capture_addr = 0;
	}

	if (driver->silent) {
		free (driver->silent);
		driver->silent = 0;
	}

	if (driver->dither_state) {
		free (driver->dither_state);
		driver->dither_state = 0;
	}
}

static int
alsa_driver_check_capabilities (alsa_driver_t *driver)

{
	return 0;
}

static int
alsa_driver_check_card_type (alsa_driver_t *driver)

{
	int err;
	snd_ctl_card_info_t *card_info;

	snd_ctl_card_info_alloca (&card_info);

	// XXX: I don't know the "right" way to do this. Which to use driver->alsa_name_playback or driver->alsa_name_capture.
	if ((err = snd_ctl_open (&driver->ctl_handle, driver->alsa_name_playback, 0)) < 0) {
		jack_error ("control open \"%s\" (%s)", driver->alsa_name_playback, snd_strerror(err));
		return -1;
	}
	
	if ((err = snd_ctl_card_info(driver->ctl_handle, card_info)) < 0) {
		jack_error ("control hardware info \"%s\" (%s)", driver->alsa_name_playback, snd_strerror (err));
		snd_ctl_close (driver->ctl_handle);
		return -1;
	}

	driver->alsa_driver = strdup(snd_ctl_card_info_get_driver (card_info));

	return alsa_driver_check_capabilities (driver);
}

static int
alsa_driver_hammerfall_hardware (alsa_driver_t *driver)

{
	driver->hw = jack_alsa_hammerfall_hw_new (driver);
	return 0;
}

static int
alsa_driver_hdsp_hardware (alsa_driver_t *driver)

{
	driver->hw = jack_alsa_hdsp_hw_new (driver);
	return 0;
}

static int
alsa_driver_ice1712_hardware (alsa_driver_t *driver)

{
        driver->hw = jack_alsa_ice1712_hw_new (driver);
        return 0;
}

static int
alsa_driver_generic_hardware (alsa_driver_t *driver)

{
	driver->hw = jack_alsa_generic_hw_new (driver);
	return 0;
}

static int
alsa_driver_hw_specific (alsa_driver_t *driver, int hw_monitoring, int hw_metering)

{
	int err;

	if (!strcmp(driver->alsa_driver, "RME9652")) {
		if ((err = alsa_driver_hammerfall_hardware (driver)) != 0) {
			return err;
		}
	} else if (!strcmp(driver->alsa_driver, "H-DSP")) {
                if ((err = alsa_driver_hdsp_hardware (driver)) !=0) {
                        return err;
                }
	} else if (!strcmp(driver->alsa_driver, "ICE1712")) {
                if ((err = alsa_driver_ice1712_hardware (driver)) !=0) {
                        return err;
                }
	} else {
	        if ((err = alsa_driver_generic_hardware (driver)) != 0) {
			return err;
		}
	}

	if (driver->hw->capabilities & Cap_HardwareMonitoring) {
		driver->has_hw_monitoring = TRUE;
		/* XXX need to ensure that this is really FALSE or TRUE or whatever*/
		driver->hw_monitoring = hw_monitoring;
	} else {
		driver->has_hw_monitoring = FALSE;
		driver->hw_monitoring = FALSE;
	}

	if (driver->hw->capabilities & Cap_ClockLockReporting) {
		driver->has_clock_sync_reporting = TRUE;
	} else {
		driver->has_clock_sync_reporting = FALSE;
	}

	if (driver->hw->capabilities & Cap_HardwareMetering) {
		driver->has_hw_metering = TRUE;
		driver->hw_metering = hw_metering;
	} else {
		driver->has_hw_metering = FALSE;
		driver->hw_metering = FALSE;
	}

	return 0;
}

static void
alsa_driver_setup_io_function_pointers (alsa_driver_t *driver)

{
	switch (driver->sample_bytes) {
	case 2:
		if (driver->interleaved) {
			driver->channel_copy = memcpy_interleave_d16_s16;
		} else {
			driver->channel_copy = memcpy_fake;
		}

		switch (driver->dither) {
			case Rectangular:
			printf("Rectangular dithering at 16 bits\n");
			driver->write_via_copy = sample_move_dither_rect_d16_sS;
			break;

			case Triangular:
			printf("Triangular dithering at 16 bits\n");
			driver->write_via_copy = sample_move_dither_tri_d16_sS;
			break;

			case Shaped:
			printf("Noise-shaped dithering at 16 bits\n");
			driver->write_via_copy = sample_move_dither_shaped_d16_sS;
			break;

			default:
			driver->write_via_copy = sample_move_d16_sS;
			break;
		}

		driver->read_via_copy = sample_move_dS_s16;
		break;

	case 4:
		if (driver->interleaved) {
			driver->channel_copy = memcpy_interleave_d32_s32;
		} else {
			driver->channel_copy = memcpy_fake;
		}
		
		switch (driver->dither) {
			case Rectangular:
			printf("Rectangular dithering at 16 bits\n");
			driver->write_via_copy = sample_move_dither_rect_d32u24_sS;
			break;

			case Triangular:
			printf("Triangular dithering at 16 bits\n");
			driver->write_via_copy = sample_move_dither_tri_d32u24_sS;
			break;

			case Shaped:
			printf("Noise-shaped dithering at 16 bits\n");
			driver->write_via_copy = sample_move_dither_shaped_d32u24_sS;
			break;

			default:
			driver->write_via_copy = sample_move_d32u24_sS;
			break;
		}

		driver->read_via_copy = sample_move_dS_s32u24;

		break;
	}
}

static int
alsa_driver_configure_stream (alsa_driver_t *driver, char *device_name,
			      const char *stream_name,
			      snd_pcm_t *handle, 
			      snd_pcm_hw_params_t *hw_params, 
			      snd_pcm_sw_params_t *sw_params, 
			      unsigned long *nchns)
{
	int err;

	if ((err = snd_pcm_hw_params_any (handle, hw_params)) < 0)  {
		jack_error ("ALSA: no playback configurations available (%s)",
			    snd_strerror (err));
		return -1;
	}

	if ((err = snd_pcm_hw_params_set_periods_integer (handle, hw_params)) < 0) {
		jack_error ("ALSA: cannot restrict period size to integral value.");
		return -1;
	}

	if ((err = snd_pcm_hw_params_set_access (handle, hw_params, SND_PCM_ACCESS_MMAP_NONINTERLEAVED)) < 0) {
		if ((err = snd_pcm_hw_params_set_access (handle, hw_params, SND_PCM_ACCESS_MMAP_INTERLEAVED)) < 0) {
			jack_error ("ALSA: mmap-based access is not possible for the %s "
				    "stream of this audio interface", stream_name);
			return -1;
		}
	}
	
	if ((err = snd_pcm_hw_params_set_format (handle, hw_params, SND_PCM_FORMAT_S32)) < 0) {
		if ((err = snd_pcm_hw_params_set_format (handle, hw_params, SND_PCM_FORMAT_S16)) < 0) {
			jack_error ("Sorry. The audio interface \"%s\""
				    "doesn't support either of the two hardware sample formats that jack can use.",
				    device_name);
			return -1;
		}
	}

	if ((err = snd_pcm_hw_params_set_rate_near (handle, hw_params, driver->frame_rate, 0)) < 0) {
		jack_error ("ALSA: cannot set sample/frame rate to %u for %s", driver->frame_rate, stream_name);
		return -1;
	}

	*nchns = snd_pcm_hw_params_get_channels_max (hw_params);

	if (*nchns > 1024) { 

		/* the hapless user is an unwitting victim of the "default"
		   ALSA PCM device, which can support up to 16 million
		   channels. since they can't be bothered to set up
		   a proper default device, limit the number of channels
		   for them to a sane default.
		*/

		jack_error ("You appear to be using the ALSA software \"plug\" layer, probably\n"
			    "a result of using the \"default\" ALSA device. This is less\n"
			    "efficient than it could be. Consider using a ~/.asoundrc file\n"
			    "to define a hardware audio device rather than using the plug layer\n");
		*nchns = 2;  
	}				

	if ((err = snd_pcm_hw_params_set_channels (handle, hw_params, *nchns)) < 0) {
		jack_error ("ALSA: cannot set channel count to %u for %s", *nchns, stream_name);
		return -1;
	}
	
	if ((err = snd_pcm_hw_params_set_period_size (handle, hw_params, driver->frames_per_cycle, 0)) < 0) {
		jack_error ("ALSA: cannot set period size to %u frames for %s", driver->frames_per_cycle, stream_name);
		return -1;
	}

	if ((err = snd_pcm_hw_params_set_periods (handle, hw_params, driver->user_nperiods, 0)) < 0) {
		jack_error ("ALSA: cannot set number of periods to %u for %s", driver->user_nperiods, stream_name);
		return -1;
	}
	
	if ((err = snd_pcm_hw_params_set_buffer_size (handle, hw_params, driver->user_nperiods * driver->frames_per_cycle)) < 0) {
		jack_error ("ALSA: cannot set buffer length to %u for %s", driver->user_nperiods * driver->frames_per_cycle, stream_name);
		return -1;
	}

	if ((err = snd_pcm_hw_params (handle, hw_params)) < 0) {
		jack_error ("ALSA: cannot set hardware parameters for %s", stream_name);
		return -1;
	}

	snd_pcm_sw_params_current (handle, sw_params);

	if ((err = snd_pcm_sw_params_set_start_threshold (handle, sw_params, 0U)) < 0) {
		jack_error ("ALSA: cannot set start mode for %s", stream_name);
		return -1;
	}

	{
		snd_pcm_uframes_t stop_th = driver->user_nperiods * driver->frames_per_cycle;
		if (driver->soft_mode) {
			stop_th = (snd_pcm_uframes_t)-1;
		}

		if ((err = snd_pcm_sw_params_set_stop_threshold (handle, sw_params, stop_th)) < 0) {
			jack_error ("ALSA: cannot set stop mode for %s", stream_name);
			return -1;
		}
	}
	
	if ((err = snd_pcm_sw_params_set_silence_threshold (handle, sw_params, 0)) < 0) {
		jack_error ("ALSA: cannot set silence threshold for %s", stream_name);
		return -1;
	}

	if ((err = snd_pcm_sw_params_set_silence_size (handle, sw_params, driver->frames_per_cycle * driver->nfragments)) < 0) {
		jack_error ("ALSA: cannot set silence size for %s", stream_name);
		return -1;
	}

	if ((err = snd_pcm_sw_params_set_avail_min (handle, sw_params, driver->frames_per_cycle)) < 0) {
		jack_error ("ALSA: cannot set avail min for %s", stream_name);
		return -1;
	}

	if ((err = snd_pcm_sw_params (handle, sw_params)) < 0) {
		jack_error ("ALSA: cannot set software parameters for %s", stream_name);
		return -1;
	}

	return 0;
}

static int 
alsa_driver_set_parameters (alsa_driver_t *driver, jack_nframes_t frames_per_cycle, jack_nframes_t user_nperiods, jack_nframes_t rate)

{
	int p_noninterleaved = 0;
	int c_noninterleaved = 0;
	snd_pcm_format_t c_format = 0;
	snd_pcm_format_t p_format = 0;
	int dir;
	unsigned int p_period_size = 0;
	unsigned int c_period_size = 0;
	unsigned int p_nfragments = 0;
	unsigned int c_nfragments = 0;
	channel_t chn;

	driver->frame_rate = rate;
	driver->frames_per_cycle = frames_per_cycle;
	driver->user_nperiods = user_nperiods;
	
	if (driver->capture_handle) {
		if (alsa_driver_configure_stream (driver, driver->alsa_name_capture, "capture", 
						  driver->capture_handle,
						  driver->capture_hw_params,
						  driver->capture_sw_params,
						  &driver->capture_nchannels)) {
			jack_error ("ALSA: cannot configure capture channel");
			return -1;
		}
	}

	if (driver->playback_handle) {
		if (alsa_driver_configure_stream (driver, driver->alsa_name_playback, "playback",
						  driver->playback_handle,
						  driver->playback_hw_params,
						  driver->playback_sw_params,
						  &driver->playback_nchannels)) {
			jack_error ("ALSA: cannot configure playback channel");
			return -1;
		}
	}
	
	/* check the fragment size, since thats non-negotiable */
	
	if (driver->playback_handle) {
		p_period_size = snd_pcm_hw_params_get_period_size (driver->playback_hw_params, &dir);
		p_nfragments = snd_pcm_hw_params_get_periods (driver->playback_hw_params, &dir);
		p_format = (snd_pcm_format_t) snd_pcm_hw_params_get_format (driver->playback_hw_params);
		p_noninterleaved = (snd_pcm_hw_params_get_access (driver->playback_hw_params) == SND_PCM_ACCESS_MMAP_NONINTERLEAVED);

		if (p_period_size != driver->frames_per_cycle) {
			jack_error ("alsa_pcm: requested an interrupt every %u frames but got %uc frames for playback",
				    driver->frames_per_cycle,p_period_size);
			return -1;
		}
	}

	if (driver->capture_handle) {
		c_period_size = snd_pcm_hw_params_get_period_size (driver->capture_hw_params, &dir);
		c_nfragments = snd_pcm_hw_params_get_periods (driver->capture_hw_params, &dir);
		c_format = (snd_pcm_format_t) snd_pcm_hw_params_get_format (driver->capture_hw_params);
		c_noninterleaved = (snd_pcm_hw_params_get_access (driver->capture_hw_params) == SND_PCM_ACCESS_MMAP_NONINTERLEAVED);
	
		if (c_period_size != driver->frames_per_cycle) {
			jack_error ("alsa_pcm: requested an interrupt every %u frames but got %uc frames for capture",
				    driver->frames_per_cycle,p_period_size);
			return -1;
		}
	}

	if (driver->capture_handle && driver->playback_handle) {
		if (p_nfragments != c_nfragments) {
			jack_error ("alsa_pcm: different period counts for playback and capture!");
			return -1;
		}

		/* Check that we are using the same sample format on both streams */
		
		if (p_format != c_format) {
			jack_error ("Sorry. The PCM device \"%s\" and \"%s\""
				    "don't support the same sample format for capture and playback."
				    "We cannot use this PCM device.", driver->alsa_name_playback, driver->alsa_name_capture );
			return -1;
		}

		/* check interleave setup */
		
		if (c_noninterleaved != p_noninterleaved) {
			jack_error ("ALSA: the playback and capture components for this PCM device differ "
				    "in their use of channel interleaving. We cannot use this PCM device.");
			return -1;
		}
		
		driver->nfragments = c_nfragments;
		driver->interleaved = !c_noninterleaved;
		driver->sample_format = c_format;

	} else if (driver->capture_handle) {

		driver->nfragments = c_nfragments;
		driver->interleaved = !c_noninterleaved;
		driver->sample_format = c_format;

	} else {

		driver->nfragments = p_nfragments;
		driver->interleaved = !p_noninterleaved;
		driver->sample_format = p_format;
	}

	driver->buffer_frames = driver->frames_per_cycle * driver->nfragments;
	driver->sample_bytes = snd_pcm_format_physical_width (driver->sample_format) / 8;

	switch (driver->sample_format) {
	case SND_PCM_FORMAT_S32_LE:
	case SND_PCM_FORMAT_S16_LE:
	case SND_PCM_FORMAT_S32_BE:
	case SND_PCM_FORMAT_S16_BE:
		break;

	default:
		jack_error ("programming error: unhandled format type");
		exit (1);
	}

	if (driver->interleaved) {
		const snd_pcm_channel_area_t *my_areas;
		snd_pcm_uframes_t offset, frames;
		if (snd_pcm_mmap_begin(driver->playback_handle, &my_areas, &offset, &frames) < 0) {
			jack_error ("ALSA: %s: mmap areas info error", driver->alsa_name_playback);
			return -1;
		}
		driver->playback_interleave_skip = my_areas[0].step / 8;
		if (snd_pcm_mmap_begin(driver->capture_handle, &my_areas, &offset, &frames) < 0) {
			jack_error ("ALSA: %s: mmap areas info error", driver->alsa_name_playback);
			return -1;
		}
		driver->capture_interleave_skip = my_areas[0].step / 8;
		driver->interleave_unit = snd_pcm_format_physical_width (driver->sample_format) / 8;
	} else {
		driver->interleave_unit = 0;  /* NOT USED */
		driver->playback_interleave_skip = snd_pcm_format_physical_width (driver->sample_format) / 8;
		driver->capture_interleave_skip = driver->playback_interleave_skip;
	}

	if (driver->playback_nchannels > driver->capture_nchannels) {
		driver->max_nchannels = driver->playback_nchannels;
		driver->user_nchannels = driver->capture_nchannels;
	} else {
		driver->max_nchannels = driver->capture_nchannels;
		driver->user_nchannels = driver->playback_nchannels;
	}

	alsa_driver_setup_io_function_pointers (driver);

	/* Allocate and initialize structures that rely on
	   the channels counts.
	*/

	/* set up the bit pattern that is used to record which
	   channels require action on every cycle. any bits that are
	   not set after the engine's process() call indicate channels
	   that potentially need to be silenced.  

	   XXX this is limited to <wordsize> channels. Use a bitset
	   type instead.
	*/

	driver->channel_done_bits = 0;

	if (driver->playback_handle) {
		driver->playback_addr = (char **) malloc (sizeof (char *) * driver->playback_nchannels);
		memset (driver->playback_addr, 0, sizeof (char *) * driver->playback_nchannels);
		driver->silent = (unsigned long *) malloc (sizeof (unsigned long) * driver->playback_nchannels);
		
		for (chn = 0; chn < driver->playback_nchannels; chn++) {
			driver->silent[chn] = 0;
		}

		for (chn = 0; chn < driver->playback_nchannels; chn++) {
			driver->channel_done_bits |= (1<<chn);
		}

		driver->dither_state = (dither_state_t *) calloc ( driver->playback_nchannels, sizeof (dither_state_t));
	}

	if (driver->capture_handle) {
		driver->capture_addr = (char **) malloc (sizeof (char *)  * driver->capture_nchannels);
		memset (driver->capture_addr, 0, sizeof (char *) * driver->capture_nchannels);
	}

	driver->clock_sync_data = (ClockSyncStatus *) malloc (sizeof (ClockSyncStatus) * 
							      (driver->capture_nchannels > driver->playback_nchannels ?
							       driver->capture_nchannels : driver->playback_nchannels));

	driver->period_usecs = (jack_time_t) floor ((((float) driver->frames_per_cycle) / driver->frame_rate) * 1000000.0f);
	driver->poll_timeout = (int) floor (1.5f * driver->period_usecs);

	if (driver->engine) {
		driver->engine->set_buffer_size (driver->engine, driver->frames_per_cycle);
	}

	return 0;
}	

#if 0
static int  /* UNUSED */
alsa_driver_reset_parameters (alsa_driver_t *driver, jack_nframes_t frames_per_cycle, jack_nframes_t user_nperiods, jack_nframes_t rate)
{
	/* XXX unregister old ports ? */
	alsa_driver_release_channel_dependent_memory (driver);
	return alsa_driver_set_parameters (driver, frames_per_cycle, user_nperiods, rate);
}
#endif

static int
alsa_driver_get_channel_addresses (alsa_driver_t *driver,
				   snd_pcm_uframes_t *capture_avail,
				   snd_pcm_uframes_t *playback_avail,
				   snd_pcm_uframes_t *capture_offset,
				   snd_pcm_uframes_t *playback_offset)
{
	unsigned long err;
	channel_t chn;

	if (capture_avail) {
		if ((err = snd_pcm_mmap_begin (driver->capture_handle, &driver->capture_areas,
					       (snd_pcm_uframes_t *) capture_offset, 
					       (snd_pcm_uframes_t *) capture_avail)) < 0) {
			jack_error ("ALSA: %s: mmap areas info error", driver->alsa_name_capture);
			return -1;
		}
		
		for (chn = 0; chn < driver->capture_nchannels; chn++) {
			const snd_pcm_channel_area_t *a = &driver->capture_areas[chn];
			driver->capture_addr[chn] = (char *) a->addr + ((a->first + a->step * *capture_offset) / 8);
		}
	} 

	if (playback_avail) {
		if ((err = snd_pcm_mmap_begin (driver->playback_handle, &driver->playback_areas, 
					       (snd_pcm_uframes_t *) playback_offset, 
					       (snd_pcm_uframes_t *) playback_avail)) < 0) {
			jack_error ("ALSA: %s: mmap areas info error ", driver->alsa_name_playback);
			return -1;
		}
		
		for (chn = 0; chn < driver->playback_nchannels; chn++) {
			const snd_pcm_channel_area_t *a = &driver->playback_areas[chn];
			driver->playback_addr[chn] = (char *) a->addr + ((a->first + a->step * *playback_offset) / 8);
		}
	} 
	
	return 0;
}
	
static int
alsa_driver_audio_start (alsa_driver_t *driver)

{
	int err;
	snd_pcm_uframes_t poffset, pavail;
	channel_t chn;

	driver->poll_last = 0;
	driver->poll_next = 0;

	if (driver->playback_handle) {
		if ((err = snd_pcm_prepare (driver->playback_handle)) < 0) {
			jack_error ("ALSA: prepare error for playback on \"%s\" (%s)", driver->alsa_name_playback, snd_strerror(err));
			return -1;
		}
	}

	if (driver->capture_handle && driver->capture_and_playback_not_synced) {
		if ((err = snd_pcm_prepare (driver->capture_handle)) < 0) {
			jack_error ("ALSA: prepare error for capture on \"%s\" (%s)", driver->alsa_name_capture, snd_strerror(err));
			return -1;
		}
	}

	if (driver->hw_monitoring) {
		driver->hw->set_input_monitor_mask (driver->hw, driver->input_monitor_mask);
	}

	if (driver->playback_handle) {
		/* fill playback buffer with zeroes, and mark 
		   all fragments as having data.
		*/
		
		pavail = snd_pcm_avail_update (driver->playback_handle);

		if (pavail != driver->buffer_frames) {
			jack_error ("ALSA: full buffer not available at start");
			return -1;
		}
	
		if (alsa_driver_get_channel_addresses (driver, 0, &pavail, 0, &poffset)) {
			return -1;
		}

		/* XXX this is cheating. ALSA offers no guarantee that we can access
		   the entire buffer at any one time. It works on most hardware
		   tested so far, however, buts its a liability in the long run. I think
		   that alsa-lib may have a better function for doing this here, where
		   the goal is to silence the entire buffer.
		*/
		
		for (chn = 0; chn < driver->playback_nchannels; chn++) {
			alsa_driver_silence_on_channel (driver, chn, driver->buffer_frames);
		}
		
		snd_pcm_mmap_commit (driver->playback_handle, poffset, driver->buffer_frames);
		
		if ((err = snd_pcm_start (driver->playback_handle)) < 0) {
			jack_error ("could not start playback (%s)", snd_strerror (err));
			return -1;
		}
	}

	if (driver->capture_handle && driver->capture_and_playback_not_synced) {
		if ((err = snd_pcm_start (driver->capture_handle)) < 0) {
			jack_error ("could not start capture (%s)", snd_strerror (err));
			return -1;
		}
	}
			
	if (driver->hw_monitoring && (driver->input_monitor_mask || driver->all_monitor_in)) {
		if (driver->all_monitor_in) {
			driver->hw->set_input_monitor_mask (driver->hw, ~0U);
		} else {
			driver->hw->set_input_monitor_mask (driver->hw, driver->input_monitor_mask);
		}
	}

	if (driver->playback_handle) {
		driver->playback_nfds = snd_pcm_poll_descriptors_count (driver->playback_handle);
	} else {
		driver->playback_nfds = 0;
	}

	if (driver->capture_handle) {
		driver->capture_nfds = snd_pcm_poll_descriptors_count (driver->capture_handle);
	} else {
		driver->capture_nfds = 0;
	}

	if (driver->pfd)
		free (driver->pfd);
	driver->pfd = (struct pollfd *) malloc (sizeof (struct pollfd) * 
		(driver->playback_nfds + driver->capture_nfds + 2));

	return 0;
}

static int
alsa_driver_audio_stop (alsa_driver_t *driver)

{
	int err;

	if (driver->playback_handle) {
		if ((err = snd_pcm_drop (driver->playback_handle)) < 0) {
			jack_error ("alsa_pcm: channel flush for playback failed (%s)", snd_strerror (err));
			return -1;
		}
	}

	if (!driver->playback_handle || driver->capture_and_playback_not_synced) {
		if (driver->capture_handle) {
			if ((err = snd_pcm_drop (driver->capture_handle)) < 0) {
				jack_error ("alsa_pcm: channel flush for capture failed (%s)", snd_strerror (err));
				return -1;
			}
		}
	}
	
	if (driver->hw_monitoring) {
		driver->hw->set_input_monitor_mask (driver->hw, 0);
	}

	return 0;
}

static int
alsa_driver_xrun_recovery (alsa_driver_t *driver)

{
	snd_pcm_status_t *status;
	int res;

	snd_pcm_status_alloca(&status);

	if (driver->capture_handle) {
		if ((res = snd_pcm_status(driver->capture_handle, status)) < 0) {
			jack_error("status error: %s", snd_strerror(res));
		}
	} else {
		if ((res = snd_pcm_status(driver->playback_handle, status)) < 0) {
			jack_error("status error: %s", snd_strerror(res));
		}
	}

	if (snd_pcm_status_get_state(status) == SND_PCM_STATE_XRUN) {
		struct timeval now, diff, tstamp;
		gettimeofday(&now, 0);
		snd_pcm_status_get_trigger_tstamp(status, &tstamp);
		timersub(&now, &tstamp, &diff);
		fprintf(stderr, "\n\n**** alsa_pcm: xrun of at least %.3f msecs\n\n", diff.tv_sec * 1000 + diff.tv_usec / 1000.0);
	}

	if (alsa_driver_audio_stop (driver) || alsa_driver_audio_start (driver)) {
		return -1;

	}
	return 0;
}	

static void
alsa_driver_silence_untouched_channels (alsa_driver_t *driver, jack_nframes_t nframes)
	
{
	channel_t chn;

	for (chn = 0; chn < driver->playback_nchannels; chn++) {
		if ((driver->channels_not_done & (1<<chn))) { 
			if (driver->silent[chn] < driver->buffer_frames) {
				alsa_driver_silence_on_channel_no_mark (driver, chn, nframes);
				driver->silent[chn] += nframes;
			}
		}
	}
}

void 
alsa_driver_set_clock_sync_status (alsa_driver_t *driver, channel_t chn, ClockSyncStatus status)

{
	driver->clock_sync_data[chn] = status;
	alsa_driver_clock_sync_notify (driver, chn, status);
}

static int under_gdb = FALSE;

static jack_nframes_t 
alsa_driver_wait (alsa_driver_t *driver, int extra_fd, int *status, float *delayed_usecs)
{
	snd_pcm_sframes_t avail = 0;
	snd_pcm_sframes_t capture_avail = 0;
	snd_pcm_sframes_t playback_avail = 0;
	int xrun_detected = FALSE;
	int need_capture;
	int need_playback;
	unsigned int i;
	jack_time_t poll_enter;
	jack_time_t poll_ret = 0;

	*status = -1;
	*delayed_usecs = 0;

	need_capture = driver->capture_handle ? 1 : 0;

	if (extra_fd >= 0) {
		need_playback = 0;
	} else {
		need_playback = driver->playback_handle ? 1 : 0;
	}

  again:
	
	while (need_playback || need_capture) {

		unsigned int p_timed_out, c_timed_out;
		unsigned int ci = 0;
		unsigned int nfds;

		nfds = 0;

		if (need_playback) {
			snd_pcm_poll_descriptors (driver->playback_handle, &driver->pfd[0], driver->playback_nfds);
			nfds += driver->playback_nfds;
		}
		
		if (need_capture) {
			snd_pcm_poll_descriptors (driver->capture_handle, &driver->pfd[nfds], driver->capture_nfds);
			ci = nfds;
			nfds += driver->capture_nfds;
		}

		/* ALSA doesn't set POLLERR in some versions of 0.9.X */
		
		for (i = 0; i < nfds; i++) {
			driver->pfd[i].events |= POLLERR;
		}

		if (extra_fd >= 0) {
			driver->pfd[nfds].fd = extra_fd;
			driver->pfd[nfds].events = POLLIN|POLLERR|POLLHUP|POLLNVAL;
			nfds++;
		}

		poll_enter = jack_get_microseconds ();

		if (poll (driver->pfd, nfds, driver->poll_timeout) < 0) {

			if (errno == EINTR) {
				printf ("poll interrupt\n");
				// this happens mostly when run
				// under gdb, or when exiting due to a signal
				if (under_gdb) {
					goto again;
				}
				*status = -2;
				return 0;
			}
			
			jack_error ("ALSA: poll call failed (%s)", strerror (errno));
			*status = -3;
			return 0;
			
		}

		poll_ret = jack_get_microseconds ();

		if (extra_fd < 0) {
			if (driver->poll_next && poll_ret > driver->poll_next) {
				*delayed_usecs = poll_ret - driver->poll_next;
			} 
			driver->poll_last = poll_ret;
			driver->poll_next = poll_ret + driver->period_usecs;
			driver->engine->transport_cycle_start (driver->engine, 
							       poll_ret);
		}

#ifdef DEBUG_WAKEUP
		fprintf (stderr, "%Lu: checked %d fds, %Lu usecs since poll entered\n", 
			 poll_ret, 
			 nfds,
			 poll_ret - poll_enter);
#endif

		/* check to see if it was the extra FD that caused us to return from poll
		 */

		if (extra_fd >= 0) {

			if (driver->pfd[nfds-1].revents == 0) {
				/* we timed out on the extra fd */

				*status = -4;
				return -1;
			} 

			/* if POLLIN was the only bit set, we're OK */

			*status = 0;
			return (driver->pfd[nfds-1].revents == POLLIN) ? 0 : -1;
		}

		p_timed_out = 0;
		
		if (need_playback) {
			for (i = 0; i < driver->playback_nfds; i++) {
				if (driver->pfd[i].revents & POLLERR) {
					xrun_detected = TRUE;
				}
				
				if (driver->pfd[i].revents == 0) {
					p_timed_out++;
#ifdef DEBUG_WAKEUP
					fprintf (stderr, "%Lu playback stream timed out\n", poll_ret);
#endif
				}
			}
			
			if (p_timed_out == 0) {
				need_playback = 0;
#ifdef DEBUG_WAKEUP
				fprintf (stderr, "%Lu playback stream ready\n", poll_ret);
#endif
			}
		}
		
		c_timed_out = 0;

		if (need_capture) {
			for (i = ci; i < nfds; i++) {
				if (driver->pfd[i].revents & POLLERR) {
					xrun_detected = TRUE;
				}
				
				if (driver->pfd[i].revents == 0) {
					c_timed_out++;
#ifdef DEBUG_WAKEUP
					fprintf (stderr, "%Lu capture stream timed out\n", poll_ret);
#endif
				}
			}
			
			if (c_timed_out == 0) {
				need_capture = 0;
#ifdef DEBUG_WAKEUP
				fprintf (stderr, "%Lu capture stream ready\n", poll_ret);
#endif
			}
		}
		
		if ((p_timed_out && (p_timed_out == driver->playback_nfds)) &&
		    (c_timed_out && (c_timed_out == driver->capture_nfds))){
			jack_error ("ALSA: poll time out, polled for %Lu usecs", poll_ret - poll_enter);
			*status = -5;
			return 0;
		}		

	}

	if (driver->capture_handle) {
		if ((capture_avail = snd_pcm_avail_update (driver->capture_handle)) < 0) {
			if (capture_avail == -EPIPE) {
				xrun_detected = TRUE;
			} else {
				jack_error ("unknown ALSA avail_update return value (%u)", capture_avail);
			}
		}
	} else {
		capture_avail = INT_MAX; /* odd, but see min() computation below */
	}

	if (driver->playback_handle) {
		if ((playback_avail = snd_pcm_avail_update (driver->playback_handle)) < 0) {
			if (playback_avail == -EPIPE) {
				xrun_detected = TRUE;
			} else {
				jack_error ("unknown ALSA avail_update return value (%u)", playback_avail);
			}
		}
	} else {
		playback_avail = INT_MAX; /* odd, but see min() computation below */
	}

	if (xrun_detected) {
		*status = alsa_driver_xrun_recovery (driver);
		return 0;
	}

	*status = 0;
	driver->last_wait_ust = poll_ret;

	avail = capture_avail < playback_avail ? capture_avail : playback_avail;

#ifdef DEBUG_WAKEUP
	fprintf (stderr, "wakup complete, avail = %lu, pavail = %lu cavail = %lu\n",
		 avail, playback_avail, capture_avail);
#endif

	/* mark all channels not done for now. read/write will change this */

	driver->channels_not_done = driver->channel_done_bits;

	/* constrain the available count to the nearest (round down) number of
	   periods.
	*/

	return avail - (avail % driver->frames_per_cycle);
}

static int
alsa_driver_null_cycle (alsa_driver_t* driver, jack_nframes_t nframes)
{
	jack_nframes_t nf;
	snd_pcm_uframes_t offset;
	snd_pcm_uframes_t contiguous;
	int chn;

	if (driver->capture_handle) {
		nf = nframes;
		offset = 0;
		while (nf) {
			
			contiguous = (nf > driver->frames_per_cycle) ? driver->frames_per_cycle : nf;
			
			if (snd_pcm_mmap_begin (driver->capture_handle, &driver->capture_areas,
						(snd_pcm_uframes_t *) &offset, 
						(snd_pcm_uframes_t *) &contiguous)) {
				return -1;
			}
		
			if (snd_pcm_mmap_commit (driver->capture_handle, offset, contiguous) < 0) {
				return -1;
			}

			nf -= contiguous;
		}
	}

	if (driver->playback_handle) {
		nf = nframes;
		offset = 0;
		while (nf) {
			contiguous = (nf > driver->frames_per_cycle) ? driver->frames_per_cycle : nf;
			
			if (snd_pcm_mmap_begin (driver->playback_handle, &driver->playback_areas,
						(snd_pcm_uframes_t *) &offset, 
						(snd_pcm_uframes_t *) &contiguous)) {
				return -1;
			}
			
			for (chn = 0; chn < driver->playback_nchannels; chn++) {
				alsa_driver_silence_on_channel (driver, chn, contiguous);
			}
		
			if (snd_pcm_mmap_commit (driver->playback_handle, offset, contiguous) < 0) {
				return -1;
			}

			nf -= contiguous;
		}
	}

	return 0;
}

static int
alsa_driver_read (alsa_driver_t *driver, jack_nframes_t nframes)
{
	snd_pcm_sframes_t contiguous;
	snd_pcm_sframes_t nread;
	snd_pcm_sframes_t offset;
	jack_default_audio_sample_t* buf;
	channel_t chn;
	JSList *node;
	jack_port_t* port;

	if (!driver->capture_handle) {
		return 0;
	}

	nread = 0;
	contiguous = 0;

	while (nframes) {
		
		contiguous = (nframes > driver->frames_per_cycle) ? driver->frames_per_cycle : nframes;
		
		if (alsa_driver_get_channel_addresses (driver, 
						       (snd_pcm_uframes_t *) &contiguous, 
						       (snd_pcm_uframes_t *) 0,
						       &offset, 0) < 0) {
			return -1;
		}
			
		for (chn = 0, node = driver->capture_ports; node; node = jack_slist_next (node), chn++) {
			
			port = (jack_port_t *) node->data;
			
			if (!jack_port_connected (port)) {
				/* no-copy optimization */
				continue;
			}

			buf = jack_port_get_buffer (port, nframes);
			alsa_driver_read_from_channel (driver, chn, buf + nread, contiguous);
		}
		
		if (snd_pcm_mmap_commit (driver->capture_handle, offset, contiguous) < 0) {
			jack_error ("alsa_pcm: could not complete read of %lu frames\n", contiguous);
			return -1;
		}

		nframes -= contiguous;
	}

	return 0;
}

static int
alsa_driver_write (alsa_driver_t* driver, jack_nframes_t nframes)
{
	channel_t chn;
	JSList *node;
	jack_default_audio_sample_t* buf;
	snd_pcm_sframes_t nwritten;
	snd_pcm_sframes_t contiguous;
	snd_pcm_sframes_t offset;
	jack_port_t *port;

	if (!driver->playback_handle) {
		return 0;
	}

	nwritten = 0;
	contiguous = 0;
	
	/* check current input monitor request status */
	
	driver->input_monitor_mask = 0;
	
	for (chn = 0, node = driver->capture_ports; node; node = jack_slist_next (node), chn++) {
		if (((jack_port_t *) node->data)->shared->monitor_requests) {
			driver->input_monitor_mask |= (1<<chn);
		}
	}

	if (driver->hw_monitoring) {
		if ((driver->hw->input_monitor_mask != driver->input_monitor_mask) && !driver->all_monitor_in) {
			driver->hw->set_input_monitor_mask (driver->hw, driver->input_monitor_mask);
		}
	}
	
	while (nframes) {
		
		contiguous = (nframes > driver->frames_per_cycle) ? driver->frames_per_cycle : nframes;
		
		if (alsa_driver_get_channel_addresses (driver, 
						       (snd_pcm_uframes_t *) 0,
						       (snd_pcm_uframes_t *) &contiguous, 
						       0, &offset) < 0) {
			return -1;
		}
		
		for (chn = 0, node = driver->playback_ports; node; node = jack_slist_next (node), chn++) {

			port = (jack_port_t *) node->data;

			if (!jack_port_connected (port)) {
				continue;
			}
			
			buf = jack_port_get_buffer (port, contiguous);

			alsa_driver_write_to_channel (driver, chn, buf + nwritten, contiguous);
		}

		if (driver->channels_not_done) {
			alsa_driver_silence_untouched_channels (driver, contiguous);
		}
		
		if (snd_pcm_mmap_commit (driver->playback_handle, offset, contiguous) < 0) {
			jack_error ("could not complete playback of %lu frames", contiguous);
			return -1;
		}

		nframes -= contiguous;
	}

	return 0;
}


static int
alsa_driver_attach (alsa_driver_t *driver, jack_engine_t *engine)
{
	char buf[32];
	channel_t chn;
	jack_port_t *port;
	int port_flags;

	driver->engine = engine;

	driver->engine->set_buffer_size (engine, driver->frames_per_cycle);
	driver->engine->set_sample_rate (engine, driver->frame_rate);

	port_flags = JackPortIsOutput|JackPortIsPhysical|JackPortIsTerminal;

	if (driver->has_hw_monitoring) {
		port_flags |= JackPortCanMonitor;
	}

	for (chn = 0; chn < driver->capture_nchannels; chn++) {

		snprintf (buf, sizeof(buf) - 1, "capture_%lu", chn+1);

		if ((port = jack_port_register (driver->client, buf, JACK_DEFAULT_AUDIO_TYPE, port_flags, 0)) == NULL) {
			jack_error ("ALSA: cannot register port for %s", buf);
			break;
		}

		if (driver->hw_metering) {
			jack_port_set_peak_function(port, driver->hw->get_hardware_peak);
			jack_port_set_power_function(port, driver->hw->get_hardware_power);
		}
		
		/* XXX fix this so that it can handle: systemic (external) latency
		*/

		jack_port_set_latency (port, driver->frames_per_cycle);

		driver->capture_ports = jack_slist_append (driver->capture_ports, port);
	}
	
	port_flags = JackPortIsInput|JackPortIsPhysical|JackPortIsTerminal;

	for (chn = 0; chn < driver->playback_nchannels; chn++) {
		jack_port_t *monitor_port;

		snprintf (buf, sizeof(buf) - 1, "playback_%lu", chn+1);

		if ((port = jack_port_register (driver->client, buf, JACK_DEFAULT_AUDIO_TYPE, port_flags, 0)) == NULL) {
			jack_error ("ALSA: cannot register port for %s", buf);
			break;
		}
		
		if (driver->hw_metering) {
			jack_port_set_peak_function(port, driver->hw->get_hardware_peak);
			jack_port_set_power_function(port, driver->hw->get_hardware_power);
		}
		
		/* XXX fix this so that it can handle: systemic (external) latency
		*/

		jack_port_set_latency (port, driver->frames_per_cycle * driver->nfragments);

		driver->playback_ports = jack_slist_append (driver->playback_ports, port);

		if (driver->with_monitor_ports) {
			snprintf (buf, sizeof(buf) - 1, "monitor_%lu", chn+1);
			
			if ((monitor_port = jack_port_register (driver->client, buf, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0)) == NULL) {
				jack_error ("ALSA: cannot register monitor port for %s", buf);
			} else {
				jack_port_tie (port, monitor_port);
			}
		}
	}

	jack_activate (driver->client);
	return 0;
}

static void
alsa_driver_detach (alsa_driver_t *driver, jack_engine_t *engine)

{
	JSList *node;

	if (driver->engine == 0) {
		return;
	}

	for (node = driver->capture_ports; node; node = jack_slist_next (node)) {
		jack_port_unregister (driver->client, ((jack_port_t *) node->data));
	}

	jack_slist_free (driver->capture_ports);
	driver->capture_ports = 0;
		
	for (node = driver->playback_ports; node; node = jack_slist_next (node)) {
		jack_port_unregister (driver->client, ((jack_port_t *) node->data));
	}

	jack_slist_free (driver->playback_ports);
	driver->playback_ports = 0;
	
	driver->engine = 0;
}

#if 0
static int  /* UNUSED */
alsa_driver_change_sample_clock (alsa_driver_t *driver, SampleClockMode mode)

{
	return driver->hw->change_sample_clock (driver->hw, mode);
}

static void  /* UNUSED */
alsa_driver_request_all_monitor_input (alsa_driver_t *driver, int yn)

{
	if (driver->hw_monitoring) {
		if (yn) {
			driver->hw->set_input_monitor_mask (driver->hw, ~0U);
		} else {
			driver->hw->set_input_monitor_mask (driver->hw, driver->input_monitor_mask);
		}
	}

	driver->all_monitor_in = yn;
}

static void  /* UNUSED */
alsa_driver_set_hw_monitoring (alsa_driver_t *driver, int yn)

{
	if (yn) {
		driver->hw_monitoring = TRUE;
		
		if (driver->all_monitor_in) {
			driver->hw->set_input_monitor_mask (driver->hw, ~0U);
		} else {
			driver->hw->set_input_monitor_mask (driver->hw, driver->input_monitor_mask);
		}
	} else {
		driver->hw_monitoring = FALSE;
		driver->hw->set_input_monitor_mask (driver->hw, 0);
	}
}

static ClockSyncStatus  /* UNUSED */
alsa_driver_clock_sync_status (channel_t chn)

{
	return Lock;
}
#endif

static void
alsa_driver_delete (alsa_driver_t *driver)

{
	JSList *node;

	for (node = driver->clock_sync_listeners; node; node = jack_slist_next (node)) {
		free (node->data);
	}
	jack_slist_free (driver->clock_sync_listeners);

	if (driver->capture_handle) {
		snd_pcm_close (driver->capture_handle);
		driver->capture_handle = 0;
	} 

	if (driver->playback_handle) {
		snd_pcm_close (driver->playback_handle);
		driver->capture_handle = 0;
	}
	
	if (driver->capture_hw_params) {
		snd_pcm_hw_params_free (driver->capture_hw_params);
		driver->capture_hw_params = 0;
	}

	if (driver->playback_hw_params) {
		snd_pcm_hw_params_free (driver->playback_hw_params);
		driver->playback_hw_params = 0;
	}
	
	if (driver->capture_sw_params) {
		snd_pcm_sw_params_free (driver->capture_sw_params);
		driver->capture_sw_params = 0;
	}
	
	if (driver->playback_sw_params) {
		snd_pcm_sw_params_free (driver->playback_sw_params);
		driver->playback_sw_params = 0;
	}

	if (driver->pfd) {
		free (driver->pfd);
	}
	
	if (driver->hw) {
		driver->hw->release (driver->hw);
		driver->hw = 0;
	}
	free(driver->alsa_name_playback);
	free(driver->alsa_name_capture);
	free(driver->alsa_driver);

	alsa_driver_release_channel_dependent_memory (driver);
	free (driver);
}

static jack_driver_t *
alsa_driver_new (char *name, char *playback_alsa_device, char *capture_alsa_device,
		 jack_client_t *client, 
		 jack_nframes_t frames_per_cycle,
		 jack_nframes_t user_nperiods,
		 jack_nframes_t rate,
		 int hw_monitoring,
		 int hw_metering,
		 int capturing,
		 int playing,
		 DitherAlgorithm dither,
		 int soft_mode, 
		 int monitor)
{
	int err;

	alsa_driver_t *driver;

	printf ("creating alsa driver ... %s|%s|%lu|%lu|%lu|%s|%s|%s\n", 
		playback_alsa_device, capture_alsa_device, frames_per_cycle, user_nperiods, rate,
		hw_monitoring ? "hwmon":"nomon", hw_metering ? "hwmeter":"swmeter", soft_mode ? "soft-mode":"rt");

	driver = (alsa_driver_t *) calloc (1, sizeof (alsa_driver_t));

	jack_driver_init ((jack_driver_t *) driver);

	driver->attach = (JackDriverAttachFunction) alsa_driver_attach;
        driver->detach = (JackDriverDetachFunction) alsa_driver_detach;
	driver->wait = (JackDriverWaitFunction) alsa_driver_wait;
	driver->read = (JackDriverReadFunction) alsa_driver_read;
	driver->write = (JackDriverReadFunction) alsa_driver_write;
	driver->null_cycle = (JackDriverNullCycleFunction) alsa_driver_null_cycle;
	driver->start = (JackDriverStartFunction) alsa_driver_audio_start;
	driver->stop = (JackDriverStopFunction) alsa_driver_audio_stop;

	driver->playback_handle = NULL;
	driver->capture_handle = NULL;
	driver->ctl_handle = 0;
	driver->hw = 0;
	driver->capture_and_playback_not_synced = FALSE;
	driver->nfragments = 0;
	driver->max_nchannels = 0;
	driver->user_nchannels = 0;
	driver->playback_nchannels = 0;
	driver->capture_nchannels = 0;
	driver->playback_addr = 0;
	driver->capture_addr = 0;
	driver->silent = 0;
	driver->all_monitor_in = FALSE;
	driver->with_monitor_ports = monitor;

	driver->clock_mode = ClockMaster; /* XXX is it? */
	driver->input_monitor_mask = 0;   /* XXX is it? */

	driver->capture_ports = 0;
	driver->playback_ports = 0;

	driver->pfd = 0;
	driver->playback_nfds = 0;
	driver->capture_nfds = 0;

	driver->dither = dither;
	driver->soft_mode = soft_mode;

	pthread_mutex_init (&driver->clock_sync_lock, 0);
	driver->clock_sync_listeners = 0;

	if (playing) {
		if (snd_pcm_open (&driver->playback_handle, playback_alsa_device, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK) < 0) {
			if (errno == -EBUSY) {
				jack_error ("the playback device \"%s\" is already in use. Please stop the application using it and "
					    "run JACK again", playback_alsa_device);
				return NULL;
			}
			driver->playback_handle = NULL;
		}
		snd_pcm_nonblock (driver->playback_handle, 0);
	} 

	if (capturing) {
		if (snd_pcm_open (&driver->capture_handle, capture_alsa_device, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK) < 0) {
			if (errno == -EBUSY) {
				jack_error ("the capture device \"%s\" is already in use. Please stop the application using it and "
					    "run JACK again", capture_alsa_device);
				return NULL;
			}
			driver->capture_handle = NULL;
		}
		snd_pcm_nonblock (driver->capture_handle, 0);
	}

	fprintf (stderr, "open\n");

	if (driver->playback_handle == NULL) {
		if (playing) {

			/* they asked for playback, but we can't do it */

			jack_error ("ALSA: Cannot open PCM device %s for playback. Falling back to capture-only mode",
				    name);

			if (driver->capture_handle == NULL) {
				/* can't do anything */
				free (driver);
				return 0;
			}
			
			playing = FALSE;
		}
	}

	if (driver->capture_handle == NULL) {
		if (capturing) {

			/* they asked for capture, but we can't do it */
			
			jack_error ("ALSA: Cannot open PCM device %s for capture. Falling back to playback-only mode",
				    name);
			
			if (driver->playback_handle == NULL) {
				/* can't do anything */
				free (driver);
				return 0;
			}

			capturing = FALSE;
		}
	}

	driver->alsa_name_playback = strdup (playback_alsa_device);
	driver->alsa_name_capture = strdup (capture_alsa_device);

	if (alsa_driver_check_card_type (driver)) {
		if (driver->capture_handle) {
			snd_pcm_close (driver->capture_handle);
		}
		if (driver->playback_handle) {
			snd_pcm_close (driver->playback_handle);
		}
		free (driver);
		return 0;
	}

	driver->playback_hw_params = 0;
	driver->capture_hw_params = 0;
	driver->playback_sw_params = 0;
	driver->capture_sw_params = 0;

	if (driver->playback_handle) {
		if ((err = snd_pcm_hw_params_malloc (&driver->playback_hw_params)) < 0) {
			jack_error ("ALSA: could no allocate playback hw params structure");
			alsa_driver_delete (driver);
			return 0;
		}

		if ((err = snd_pcm_sw_params_malloc (&driver->playback_sw_params)) < 0) {
			jack_error ("ALSA: could no allocate playback sw params structure");
			alsa_driver_delete (driver);
			return 0;
		}
	}

	if (driver->capture_handle) {
		if ((err = snd_pcm_hw_params_malloc (&driver->capture_hw_params)) < 0) {
			jack_error ("ALSA: could no allocate capture hw params structure");
			alsa_driver_delete (driver);
			return 0;
		}

		if ((err = snd_pcm_sw_params_malloc (&driver->capture_sw_params)) < 0) {
			jack_error ("ALSA: could no allocate capture sw params structure");
			alsa_driver_delete (driver);
			return 0;
		}
	}

	if (alsa_driver_set_parameters (driver, frames_per_cycle, user_nperiods, rate)) {
		alsa_driver_delete (driver);
		return 0;
	}

	driver->capture_and_playback_not_synced = FALSE;

	if (driver->capture_handle && driver->playback_handle) {
		if (snd_pcm_link (driver->capture_handle, driver->playback_handle) != 0) {
			driver->capture_and_playback_not_synced = TRUE;
		} 
	}

	alsa_driver_hw_specific (driver, hw_monitoring, hw_metering);

	driver->client = client;

	return (jack_driver_t *) driver;
}

int
alsa_driver_listen_for_clock_sync_status (alsa_driver_t *driver, 
					  ClockSyncListenerFunction func,
					  void *arg)
{
	ClockSyncListener *csl;

	csl = (ClockSyncListener *) malloc (sizeof (ClockSyncListener));
	csl->function = func;
	csl->arg = arg;
	csl->id = driver->next_clock_sync_listener_id++;
	
	pthread_mutex_lock (&driver->clock_sync_lock);
	driver->clock_sync_listeners = jack_slist_prepend (driver->clock_sync_listeners, csl);
	pthread_mutex_unlock (&driver->clock_sync_lock);
	return csl->id;
}

int
alsa_driver_stop_listening_to_clock_sync_status (alsa_driver_t *driver, unsigned int which)

{
	JSList *node;
	int ret = -1;
	pthread_mutex_lock (&driver->clock_sync_lock);
	for (node = driver->clock_sync_listeners; node; node = jack_slist_next (node)) {
		if (((ClockSyncListener *) node->data)->id == which) {
			driver->clock_sync_listeners = jack_slist_remove_link (driver->clock_sync_listeners, node);
			free (node->data);
			jack_slist_free_1 (node);
			ret = 0;
			break;
		}
	}
	pthread_mutex_unlock (&driver->clock_sync_lock);
	return ret;
}

void 
alsa_driver_clock_sync_notify (alsa_driver_t *driver, channel_t chn, ClockSyncStatus status)
{
	JSList *node;

	pthread_mutex_lock (&driver->clock_sync_lock);
	for (node = driver->clock_sync_listeners; node; node = jack_slist_next (node)) {
		ClockSyncListener *csl = (ClockSyncListener *) node->data;
		csl->function (chn, status, csl->arg);
	}
	pthread_mutex_unlock (&driver->clock_sync_lock);

}

static void
alsa_usage ()
{
	fprintf (stderr, "\n"
"ALSA driver arguments:\n"
"    -h,--help    \tprint this message\n"
"    -d,--device <name> \tALSA device name (default: \"default\")\n"
"    -r,--rate <n>      \tsample rate (default: 48000)\n"
"    -p,--period <n>    \tframes per period (default: 1024)\n"
"    -n,--nperiods <n>  \tnumber of periods in hardware buffer (default: 2)\n"
"    -H,--hwmon   \tuse hardware monitoring, if available (default: no)\n"
"    -M,--hwmeter \tuse hardware metering, if available (default: no)\n"
"    -D,--duplex  \tduplex I/O (default: yes)\n"
"    -C,--capture [name] \tcapture input and optionally set the capture device (default: duplex)\n"
"    -P,--playback [name] \tplayback output and optionally set the playback device (default: duplex)\n"
"    -s,--softmode\tsoft-mode, no xrun handling (default: off)\n"
"    -m,--monitor \tprovide monitor ports for the output (default: off)\n"
"    -z,--dither  \tdithering mode:\n"
"        -zn,--dither=none (off, the default)\n"
"        -zr,--dither=rectangular\n"
"        -zs,--dither=shaped\n"
"        -zt,--dither=triangular\n"
"\n");
}

static void
alsa_error (char *type, char *value)
{
	fprintf (stderr, "ALSA driver: unknown %s: `%s'\n", type, value);
	alsa_usage();
}

static int
dither_opt (char c, DitherAlgorithm* dither)
{
	switch (*optarg) {
	case '-':
	case 'n':
		*dither = None;
		break;
		
	case 'r':
		*dither = Rectangular;
		break;
		
	case 's':
		*dither = Shaped;
		break;
		
	case 't':
		*dither = Triangular;
		break;
		
	default:
		alsa_error ("illegal dithering mode", "");
		return -1;
	}
	return 0;
}


/* DRIVER "PLUGIN" INTERFACE */

const char driver_client_name[] = "alsa_pcm";

jack_driver_t *
driver_initialize (jack_client_t *client, int argc, char **argv)
{
	jack_nframes_t srate = 48000;
	jack_nframes_t frames_per_interrupt = 1024;
	unsigned long user_nperiods = 2;
	char *playback_pcm_name = "default";
	char *capture_pcm_name = "default";
	int hw_monitoring = FALSE;
	int hw_metering = FALSE;
	int capture = FALSE;
	int playback = FALSE;
	int soft_mode = FALSE;
	int monitor = FALSE;
	DitherAlgorithm dither = None;
	int opt;
	char *envvar;
	char optstring[2];		/* string made from opt char */
	struct option long_options[] = 
	{ 
		{ "capture",	2, NULL, 'C' },
		{ "duplex",	0, NULL, 'D' },
		{ "device",	1, NULL, 'd' },
		{ "hwmon",	0, NULL, 'H' },
		{ "hwmeter",	0, NULL, 'M' },
		{ "help",	0, NULL, 'h' },
		{ "playback",	2, NULL, 'P' },
		{ "period",	1, NULL, 'p' },
		{ "rate",	1, NULL, 'r' },
		{ "nperiods",	1, NULL, 'n' },
		{ "softmode",	1, NULL, 's' },
		{ "dither",	2, NULL, 'z' },
		{ "monitor",    0, NULL, 'm' },
		{ 0, 0, 0, 0 }
	};

	/* before we do anything else, see if there are environment variables
	   for each parameter.
	*/
	
	if ((envvar = getenv ("JACK_ALSA_DEVICE")) != NULL) {
		playback_pcm_name = envvar;
		capture_pcm_name = envvar;
	}

	if ((envvar = getenv ("JACK_ALSA_HWMON")) != NULL) {
		hw_monitoring = TRUE;
	}

	if ((envvar = getenv ("JACK_ALSA_SOFTMODE")) != NULL) {
		soft_mode = TRUE;
	}
	
	if ((envvar = getenv ("JACK_ALSA_PERIOD_FRAMES")) != NULL) {
		frames_per_interrupt = atoi (envvar);
	}

	if ((envvar = getenv ("JACK_ALSA_PERIODS")) != NULL) {
		user_nperiods = atoi (envvar);
	}

	if ((envvar = getenv ("JACK_ALSA_SRATE")) != NULL) {
		srate = atoi (envvar);
	}

	if ((envvar = getenv ("JACK_ALSA_DITHER")) != NULL) {
		if (dither_opt (*envvar, &dither)) {
			return NULL;
		}
	}

	if ((envvar = getenv ("JACK_ALSA_CAPTURE")) != NULL) {
		capture = atoi (envvar);
	}

	if ((envvar = getenv ("JACK_ALSA_PLAYBACK")) != NULL) {
		playback = atoi (envvar);
	}
		
	if ((envvar = getenv ("JACK_ALSA_MONITOR")) != NULL) {
		monitor = atoi (envvar);
	}
		
	/*
	 * Setting optind back to zero is a hack to reinitialize a new
	 * getopts() loop.  See declaration in <getopt.h>.
	 */

	optind = 0;
	opterr = 0;

	while ((opt = getopt_long(argc, argv, "-C::Dd:HMP::p:r:n:msz::",
				  long_options, NULL))
	       != EOF) {
		switch (opt) {

		case 'C':
			capture = TRUE;
			if (optarg != NULL) {
				capture_pcm_name = optarg;
			}
			break;

		case 'D':
			capture = TRUE;
			playback = TRUE;
			break;

		case 'd':
			playback_pcm_name = optarg;
			capture_pcm_name = optarg;
			break;

		case 'H':
			hw_monitoring = TRUE;
			break;

		case 'h':
			alsa_usage();
			return NULL;

		case 'm':
			monitor = TRUE;
			break;

		case 'M':
			hw_metering = TRUE;
			break;
			
		case 'P':
			playback = TRUE;
			if (optarg != NULL) {
				playback_pcm_name = optarg;
			}
			break;

		case 'p':
			frames_per_interrupt = atoi(optarg);
			break;
				
		case 'r':
			srate = atoi(optarg);
			break;
				
		case 'n':
			user_nperiods = atoi(optarg);
			break;
				
		case 's':
			soft_mode = TRUE;
			break;

		case 'z':
			if (optarg == NULL) {
				dither = None;
			} else {
				if (dither_opt (*optarg, &dither)) {
					return NULL;
				}
			}
			break;

		/* the rest is error handling: */
		case 1:			/* not an option */
			alsa_error("parameter", optarg);
			return NULL;
				
		default:		/* unrecognized option */
			optstring[0] = (char) optopt;
			optstring[1] = '\0';
			alsa_error("option", optstring);
			return NULL;
		}
	}
			
	/* duplex is the default */
	if (!capture && !playback) {
		capture = TRUE;
		playback = TRUE;
	}

	return alsa_driver_new ("alsa_pcm", playback_pcm_name, capture_pcm_name, client, frames_per_interrupt, 
				user_nperiods, srate, hw_monitoring, hw_metering, capture,
			        playback, dither, soft_mode, monitor);
}

void
driver_finish (jack_driver_t *driver)
{
	alsa_driver_delete ((alsa_driver_t *) driver);
}
