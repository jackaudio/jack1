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
#include <glib.h>
#include <stdarg.h>
#include <asm/msr.h>

#include <jack/alsa_driver.h>
#include <jack/types.h>
#include <jack/internal.h>
#include <jack/engine.h>
#include <jack/hammerfall.h>
#include <jack/generic.h>

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

	if ((err = snd_ctl_open (&driver->ctl_handle, driver->alsa_name, 0)) < 0) {
		jack_error ("control open \"%s\" (%s)", driver->alsa_name, snd_strerror(err));
		return -1;
	}
	
	if ((err = snd_ctl_card_info(driver->ctl_handle, card_info)) < 0) {
		jack_error ("control hardware info \"%s\" (%s)", driver->alsa_name, snd_strerror (err));
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
alsa_driver_generic_hardware (alsa_driver_t *driver)

{
	driver->hw = jack_alsa_generic_hw_new (driver);
	return 0;
}

static int
alsa_driver_hw_specific (alsa_driver_t *driver, int hw_monitoring)

{
	int err;

	if (!strcmp(driver->alsa_driver, "RME9652")) {
		if ((err = alsa_driver_hammerfall_hardware (driver)) != 0) {
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
		
		driver->write_via_copy = sample_move_d16_sS;
		driver->read_via_copy = sample_move_dS_s16;
		break;

	case 4:
		if (driver->interleaved) {
			driver->channel_copy = memcpy_interleave_d32_s32;
		} else {
			driver->channel_copy = memcpy_fake;
		}
		
		driver->write_via_copy = sample_move_d32u24_sS;
		driver->read_via_copy = sample_move_dS_s32u24;

		break;
	}
}

static int
alsa_driver_configure_stream (alsa_driver_t *driver, 
			      const char *stream_name,
			      snd_pcm_t *handle, 
			      snd_pcm_hw_params_t *hw_params, 
			      snd_pcm_sw_params_t *sw_params, 
			      unsigned long *nchns)
{
	int err;

	if ((err = snd_pcm_hw_params_any (handle, hw_params)) < 0)  {
		jack_error ("ALSA: no playback configurations available");
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
	
	if ((err = snd_pcm_hw_params_set_format (handle, hw_params, SND_PCM_FORMAT_S32_LE)) < 0) {
		if ((err = snd_pcm_hw_params_set_format (handle, hw_params, SND_PCM_FORMAT_S16_LE)) < 0) {
			jack_error ("Sorry. The audio interface \"%s\""
				  "doesn't support either of the two hardware sample formats that ardour can use.",
				  driver->alsa_name);
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

	if ((err = snd_pcm_sw_params_set_start_threshold (handle, sw_params, ~0U)) < 0) {
		jack_error ("ALSA: cannot set start mode for %s", stream_name);
		return -1;
	}

	if ((err = snd_pcm_sw_params_set_stop_threshold (handle, sw_params, ~0U)) < 0) {
		jack_error ("ALSA: cannot set stop mode for %s", stream_name);
		return -1;
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
alsa_driver_set_parameters (alsa_driver_t *driver, nframes_t frames_per_cycle, nframes_t user_nperiods, nframes_t rate)

{
	int p_noninterleaved;
	int c_noninterleaved;
	snd_pcm_format_t c_format, p_format;
	int dir;
	unsigned int p_period_size, c_period_size;
	unsigned int p_nfragments, c_nfragments;
	channel_t chn;

	driver->frame_rate = rate;
	driver->frames_per_cycle = frames_per_cycle;
	driver->user_nperiods = user_nperiods;
	
	if (alsa_driver_configure_stream (driver, "capture",
					  driver->capture_handle,
					  driver->capture_hw_params,
					  driver->capture_sw_params,
					  &driver->capture_nchannels)) {
		jack_error ("ALSA: cannot configure capture channel");
		return -1;
	}
	
	if (alsa_driver_configure_stream (driver, "playback",
					  driver->playback_handle,
					  driver->playback_hw_params,
					  driver->playback_sw_params,
					  &driver->playback_nchannels)) {
		jack_error ("ALSA: cannot configure playback channel");
		return -1;
	}
	
	/* check the fragment size, since thats non-negotiable */
	
	p_period_size = snd_pcm_hw_params_get_period_size (driver->playback_hw_params, &dir);
	c_period_size = snd_pcm_hw_params_get_period_size (driver->capture_hw_params, &dir);
	
	if (c_period_size != driver->frames_per_cycle || p_period_size != driver->frames_per_cycle) {
		jack_error ("alsa_pcm: requested an interrupt every %u frames but got %uc%up frames",
			  driver->frames_per_cycle, c_period_size, p_period_size);
		return -1;
	}

	p_nfragments = snd_pcm_hw_params_get_periods (driver->playback_hw_params, &dir);
	c_nfragments = snd_pcm_hw_params_get_periods (driver->capture_hw_params, &dir);

	if (p_nfragments != c_nfragments) {
		jack_error ("alsa_pcm: different period counts for playback and capture!");
		return -1;
	}

	driver->nfragments = c_nfragments;
	driver->buffer_frames = driver->frames_per_cycle * driver->nfragments;

	/* Check that we are using the same sample format on both streams */

	p_format = (snd_pcm_format_t) snd_pcm_hw_params_get_format (driver->playback_hw_params);
	c_format = (snd_pcm_format_t) snd_pcm_hw_params_get_format (driver->capture_hw_params);

	if (p_format != c_format) {
		jack_error ("Sorry. The audio interface \"%s\""
			  "doesn't support the same sample format for capture and playback."
			  "this JACK/ALSA driver cannot use this hardware.", driver->alsa_name);
		return -1;
	}

	driver->sample_format = p_format;
	driver->sample_bytes = snd_pcm_format_physical_width (driver->sample_format) / 8;
	driver->bytes_per_cycle = driver->sample_bytes * driver->frames_per_cycle;

	switch (driver->sample_format) {
	case SND_PCM_FORMAT_S32_LE:
	case SND_PCM_FORMAT_S16_LE:
		break;

	default:
		jack_error ("programming error: unhandled format type");
		exit (1);
	}

	/* check interleave setup */

	p_noninterleaved = (snd_pcm_hw_params_get_access (driver->playback_hw_params) == SND_PCM_ACCESS_MMAP_NONINTERLEAVED);
	c_noninterleaved = (snd_pcm_hw_params_get_access (driver->capture_hw_params) == SND_PCM_ACCESS_MMAP_NONINTERLEAVED);

	if (c_noninterleaved != p_noninterleaved) {
		jack_error ("ALSA: the playback and capture components of this audio interface differ "
			  "in their use of channel interleaving. Ardour cannot use this h/w.");
		return -1;
	}

	driver->interleaved = !c_noninterleaved;

	if (driver->interleaved) {
		driver->interleave_unit = snd_pcm_format_physical_width (driver->sample_format) / 8;
		driver->playback_interleave_skip = driver->interleave_unit * driver->playback_nchannels;
		driver->capture_interleave_skip = driver->interleave_unit * driver->capture_nchannels;
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

	driver->playback_addr = (char **) malloc (sizeof (char *) * driver->playback_nchannels);
	driver->capture_addr = (char **) malloc (sizeof (char *)  * driver->capture_nchannels);

	memset (driver->playback_addr, 0, sizeof (char *) * driver->playback_nchannels);
	memset (driver->capture_addr, 0, sizeof (char *) * driver->capture_nchannels);
	
	driver->silent = (unsigned long *) malloc (sizeof (unsigned long) * driver->playback_nchannels);

	for (chn = 0; chn < driver->playback_nchannels; chn++) {
		driver->silent[chn] = 0;
	}

	driver->clock_sync_data = (ClockSyncStatus *) malloc (sizeof (ClockSyncStatus) * 
							      driver->capture_nchannels > driver->playback_nchannels ?
							      driver->capture_nchannels : driver->playback_nchannels);
	
	/* set up the bit pattern that is used to record which
	   channels require action on every cycle. any bits that are
	   not set after the engine's process() call indicate channels
	   that potentially need to be silenced.  

	   XXX this is limited to <wordsize> channels. Use a bitset
	   type instead.
	*/

	driver->channel_done_bits = 0;
	for (chn = 0; chn < driver->playback_nchannels; chn++) {
		driver->channel_done_bits |= (1<<chn);
	}

	driver->period_interval = (unsigned long) floor ((((float) driver->frames_per_cycle) / driver->frame_rate) * 1000.0);

	if (driver->engine) {
		driver->engine->set_buffer_size (driver->engine, driver->frames_per_cycle);
	}

	return 0;
}	

static int
alsa_driver_reset_parameters (alsa_driver_t *driver, nframes_t frames_per_cycle, nframes_t user_nperiods, nframes_t rate)
{
	/* XXX unregister old ports ? */
	alsa_driver_release_channel_dependent_memory (driver);
	return alsa_driver_set_parameters (driver, frames_per_cycle, user_nperiods, rate);
}

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
			jack_error ("ALSA-HW: %s: mmap areas info error", driver->alsa_name);
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
			jack_error ("ALSA-HW: %s: mmap areas info error ", driver->alsa_name);
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

	if ((err = snd_pcm_prepare (driver->playback_handle)) < 0) {
		jack_error ("ALSA-HW: prepare error for playback on \"%s\" (%s)", driver->alsa_name, snd_strerror(err));
		return -1;
	}

	if (driver->capture_and_playback_not_synced) {
		if ((err = snd_pcm_prepare (driver->capture_handle)) < 0) {
			jack_error ("ALSA-HW: prepare error for capture on \"%s\" (%s)", driver->alsa_name, snd_strerror(err));
			return -1;
		}
	}

	if (driver->hw_monitoring) {
		driver->hw->set_input_monitor_mask (driver->hw, driver->input_monitor_mask);
	}

	/* fill playback buffer with zeroes, and mark 
	   all fragments as having data.
	*/
	
	pavail = snd_pcm_avail_update (driver->playback_handle);

	if (pavail != driver->buffer_frames) {
		jack_error ("ALSA-HW: full buffer not available at start");
		return -1;
	}

	if (alsa_driver_get_channel_addresses (driver, 0, &pavail, 0, &poffset)) {
		return -1;
	}

	for (chn = 0; chn < driver->playback_nchannels; chn++) {
		alsa_driver_silence_on_channel (driver, chn, driver->buffer_frames);
	}

	snd_pcm_mmap_commit (driver->playback_handle, poffset, driver->buffer_frames);

	if ((err = snd_pcm_start (driver->playback_handle)) < 0) {
		jack_error ("could not start playback (%s)", snd_strerror (err));
		return -1;
	}

	if (driver->capture_and_playback_not_synced) {
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

	driver->playback_nfds = snd_pcm_poll_descriptors_count (driver->playback_handle);
	driver->capture_nfds = snd_pcm_poll_descriptors_count (driver->capture_handle);
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

	if ((err = snd_pcm_drop (driver->playback_handle)) < 0) {
		jack_error ("alsa_pcm: channel flush for playback failed (%s)", snd_strerror (err));
		return -1;
	}

	if (driver->capture_and_playback_not_synced) {
		if ((err = snd_pcm_drop (driver->capture_handle)) < 0) {
			jack_error ("alsa_pcm: channel flush for capture failed (%s)", snd_strerror (err));
			return -1;
		}
	}
	
	driver->hw->set_input_monitor_mask (driver->hw, 0);

	return 0;
}

static int
alsa_driver_xrun_recovery (alsa_driver_t *driver)

{
	snd_pcm_sframes_t capture_delay;
	int err;

	if ((err = snd_pcm_delay (driver->capture_handle, &capture_delay))) {
		jack_error ("alsa_pcm: cannot determine capture delay (%s)", snd_strerror (err));
		exit (1);
	}

	fprintf (stderr, "alsa_pcm: xrun of %lu frames, (%.3f msecs)\n", capture_delay,
		 ((float) capture_delay / (float) driver->frame_rate) * 1000.0);
	
#if ENGINE
	if (!engine->xrun_recoverable ()) {
		/* don't report an error here, its distracting */
		return -1;
	} 
#endif

	if (alsa_driver_audio_stop (driver) || alsa_driver_audio_start (driver)) {
		return -1;

	}

	return 0;
}	

static void
alsa_driver_silence_untouched_channels (alsa_driver_t *driver, nframes_t nframes)
	
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

static nframes_t 
alsa_driver_wait (alsa_driver_t *driver, int fd, int *status)
{
	snd_pcm_sframes_t avail = 0;
	snd_pcm_sframes_t capture_avail = 0;
	snd_pcm_sframes_t playback_avail = 0;
	int xrun_detected;
	int need_capture;
	int need_playback;
	int i;

	*status = -1;
	need_capture = 1;
	if (fd >= 0) {
		need_playback = 0;
	} else {
		need_playback = 1;
	}

  again:
	
	while (need_playback || need_capture) {

		int p_timed_out, c_timed_out;
		int ci = 0;
		int nfds;

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
			driver->pfd[nfds].events |= POLLERR;
		}

		if (fd >= 0) {
			driver->pfd[nfds].fd = fd;
			driver->pfd[nfds].events = POLLIN|POLLERR|POLLHUP|POLLNVAL;
			nfds++;
		}

		if (poll (driver->pfd, nfds, 1000) < 0) {
			if (errno == EINTR) {
				printf ("poll interrupt\n");
				// this happens mostly when run
				// under gdb, or when exiting due to a signal
				if (under_gdb) {
					goto again;
				}
				return 0;
			}
			
			jack_error ("ALSA::Device: poll call failed (%s)", strerror (errno));
			return 0;
			
		}

		/* check to see if it was the extra FD that caused us to return from poll
		 */

		if (fd >= 0) {
			if (driver->pfd[nfds-1].revents == 0) {
				/* we timed out on the extra fd */
				return -1;
			} else {
				/* if POLLIN was the only bit set, we're OK */
				*status = 0;
				return (driver->pfd[nfds-1].revents == POLLIN) ? 0 : -1;
			}
		}

		if (driver->engine) {
			struct timeval tv;
			gettimeofday (&tv, NULL);
			driver->engine->control->time.microseconds = tv.tv_sec * 1000000 + tv.tv_usec;
		}

		p_timed_out = 0;
		
		if (need_playback) {
			for (i = 0; i < driver->playback_nfds; i++) {
				if (driver->pfd[i].revents & POLLERR) {
					jack_error ("ALSA: poll reports error on playback stream.");
					return 0;
				}
				
				if (driver->pfd[i].revents == 0) {
					p_timed_out++;
				}
			}

			if (p_timed_out == 0) {
				need_playback = 0;
			}
		}
		
		c_timed_out = 0;

		if (need_capture) {
			for (i = ci; i < nfds; i++) {
				if (driver->pfd[i].revents & POLLERR) {
					jack_error ("ALSA: poll reports error on capture stream.");
					return 0;
				}
				
				if (driver->pfd[i].revents == 0) {
					c_timed_out++;
				}
			}
			
			if (c_timed_out == 0) {
				need_capture = 0;
			}
		}
		
		if (p_timed_out == driver->playback_nfds && c_timed_out == driver->capture_nfds) {
			jack_error ("ALSA: poll time out");
			return 0;
		}		

	}

	xrun_detected = FALSE;
	
	if ((capture_avail = snd_pcm_avail_update (driver->capture_handle)) < 0) {
		if (capture_avail == -EPIPE) {
			xrun_detected = TRUE;
		} else {
			jack_error ("unknown ALSA avail_update return value (%u)", capture_avail);
		}
	}

	if ((playback_avail = snd_pcm_avail_update (driver->playback_handle)) < 0) {
		if (playback_avail == -EPIPE) {
			xrun_detected = TRUE;
		} else {
			jack_error ("unknown ALSA avail_update return value (%u)", playback_avail);
		}
	}

	if (xrun_detected) {
		*status = alsa_driver_xrun_recovery (driver);
		return 0;
	}

	*status = 0;

	avail = capture_avail < playback_avail ? capture_avail : playback_avail;
	
	/* constrain the available count to the nearest (round down) number of
	   periods.
	*/

	return avail - (avail % driver->frames_per_cycle);
}

static int
alsa_driver_process (alsa_driver_t *driver, nframes_t nframes)
{
	snd_pcm_sframes_t contiguous = 0;
	snd_pcm_sframes_t capture_avail = 0;
	snd_pcm_sframes_t playback_avail = 0;
	snd_pcm_uframes_t capture_offset = 0;
	snd_pcm_uframes_t playback_offset = 0;
	channel_t chn;
	GSList *node;
	jack_engine_t *engine = driver->engine;

	while (nframes) {

		capture_avail = (nframes > driver->frames_per_cycle) ? driver->frames_per_cycle : nframes;
		playback_avail = (nframes > driver->frames_per_cycle) ? driver->frames_per_cycle : nframes;
		
		if (alsa_driver_get_channel_addresses (driver, 
						       (snd_pcm_uframes_t *) &capture_avail, 
						       (snd_pcm_uframes_t *) &playback_avail,
						       &capture_offset, &playback_offset) < 0) {
			return -1;
		}

		contiguous = capture_avail < playback_avail ? capture_avail : playback_avail;

		driver->channels_not_done = driver->channel_done_bits;

		if (engine->process_lock (engine) == 0) {

			channel_t chn;
			jack_port_t *port;
			GSList *node;
			int ret;

			for (chn = 0, node = driver->capture_ports; node; node = g_slist_next (node), chn++) {
				
				port = (jack_port_t *) node->data;
				
				if (!jack_port_connected (port)) {
					continue;
				}
				
				alsa_driver_read_from_channel (driver, chn, jack_port_get_buffer (port, nframes), nframes);
			}

			snd_pcm_mmap_commit (driver->capture_handle, capture_offset, contiguous);
			
			if ((ret = engine->process (engine, contiguous)) != 0) {
				engine->process_unlock (engine);
				alsa_driver_audio_stop (driver);
				if (ret > 0) {
					engine->post_process (engine);
				}
				return ret;
			}

			/* now move data from ports to channels */
			
			for (chn = 0, node = driver->playback_ports; node; node = g_slist_next (node), chn++) {
				
				jack_port_t *port = (jack_port_t *) node->data;

				if (!jack_port_connected (port)) {
					continue;
				}

				alsa_driver_write_to_channel (driver, chn, jack_port_get_buffer (port, contiguous), contiguous);
			}
			
			engine->process_unlock (engine);
		} 

		/* Now handle input monitoring */
		
		driver->input_monitor_mask = 0;
		
		for (chn = 0, node = driver->capture_ports; node; node = g_slist_next (node), chn++) {
			if (((jack_port_t *) node->data)->shared->monitor_requests) {
				driver->input_monitor_mask |= (1<<chn);
			}
		}
			
		if (!driver->hw_monitoring) {

			if (driver->all_monitor_in) {
				for (chn = 0; chn < driver->playback_nchannels; chn++) {
					alsa_driver_copy_channel (driver, chn, chn, contiguous);
				}
			} else if (driver->input_monitor_mask) {
				for (chn = 0; chn < driver->playback_nchannels; chn++) {
					if (driver->input_monitor_mask & (1<<chn)) {
						alsa_driver_copy_channel (driver, chn, chn, contiguous);
					}
				}
			}

		} else {

			if ((driver->hw->input_monitor_mask != driver->input_monitor_mask) && !driver->all_monitor_in) {
				driver->hw->set_input_monitor_mask (driver->hw, driver->input_monitor_mask);
			} 
		}

		if (driver->channels_not_done) {
			alsa_driver_silence_untouched_channels (driver, contiguous);
		}

		snd_pcm_mmap_commit (driver->playback_handle, playback_offset, contiguous);

		nframes -= contiguous;
	}
	
	engine->post_process (engine);
	return 0;
}

static void
alsa_driver_attach (alsa_driver_t *driver, jack_engine_t *engine)

{
	char buf[32];
	channel_t chn;
	jack_port_t *port;

	driver->engine = engine;

	driver->engine->set_buffer_size (engine, driver->frames_per_cycle);
	driver->engine->set_sample_rate (engine, driver->frame_rate);

	/* Now become a client of the engine */

	if ((driver->client = jack_driver_become_client ("alsa_pcm")) == NULL) {
		jack_error ("ALSA: cannot become client");
		return;
	}

	for (chn = 0; chn < driver->capture_nchannels; chn++) {
		snprintf (buf, sizeof(buf) - 1, "in_%lu", chn+1);
		port = jack_port_register (driver->client, buf, 
					   JACK_DEFAULT_AUDIO_TYPE,
					   JackPortIsOutput|JackPortIsPhysical|JackPortCanMonitor, 0);
		if (port == 0) {
			jack_error ("ALSA: cannot register port for %s", buf);
			break;
		}

		/* XXX fix this so that it can handle: systemic (external) latency
		*/

		jack_port_set_latency (port, driver->frames_per_cycle * driver->nfragments);

		driver->capture_ports = g_slist_append (driver->capture_ports, port);
	}

	for (chn = 0; chn < driver->playback_nchannels; chn++) {
		snprintf (buf, sizeof(buf) - 1, "out_%lu", chn+1);
		port = jack_port_register (driver->client, buf, 
					    JACK_DEFAULT_AUDIO_TYPE,
					    JackPortIsInput|JackPortIsPhysical, 0);
		if (port == 0) {
			jack_error ("ALSA: cannot register port for %s", buf);
			break;
		}
		
		/* XXX fix this so that it can handle: systemic (external) latency
		*/

		jack_port_set_latency (port, driver->frames_per_cycle * driver->nfragments);

		driver->playback_ports = g_slist_append (driver->playback_ports, port);
	}

	printf ("ALSA: ports registered, starting driver\n");

	jack_activate (driver->client);
}

static void
alsa_driver_detach (alsa_driver_t *driver, jack_engine_t *engine)

{
	GSList *node;

	if (driver->engine == 0) {
		return;
	}

	for (node = driver->capture_ports; node; node = g_slist_next (node)) {
		jack_port_unregister (driver->client, ((jack_port_t *) node->data));
	}

	g_slist_free (driver->capture_ports);
	driver->capture_ports = 0;
		
	for (node = driver->playback_ports; node; node = g_slist_next (node)) {
		jack_port_unregister (driver->client, ((jack_port_t *) node->data));
	}

	g_slist_free (driver->playback_ports);
	driver->playback_ports = 0;
	
	driver->engine = 0;
}

static int
alsa_driver_change_sample_clock (alsa_driver_t *driver, SampleClockMode mode)

{
	return driver->hw->change_sample_clock (driver->hw, mode);
}

static void
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

static void
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

static ClockSyncStatus
alsa_driver_clock_sync_status (channel_t chn)

{
	return Lock;
}

static void
alsa_driver_delete (alsa_driver_t *driver)

{
	GSList *node;

	for (node = driver->clock_sync_listeners; node; node = g_slist_next (node)) {
		free (node->data);
	}
	g_slist_free (driver->clock_sync_listeners);

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
	free(driver->alsa_name);
	free(driver->alsa_driver);

	alsa_driver_release_channel_dependent_memory (driver);
	free (driver);
}

static jack_driver_t *
alsa_driver_new (char *name, char *alsa_device,
		 nframes_t frames_per_cycle,
		 nframes_t user_nperiods,
		 nframes_t rate,
		 int hw_monitoring)
{
	int err;

	alsa_driver_t *driver;

	printf ("creating alsa driver ... %s|%lu|%lu|%lu|%s\n", 
		alsa_device, frames_per_cycle, user_nperiods, rate,
		hw_monitoring ? "hwmon":"swmon");

	driver = (alsa_driver_t *) calloc (1, sizeof (alsa_driver_t));

	jack_driver_init ((jack_driver_t *) driver);

	driver->attach = (JackDriverAttachFunction) alsa_driver_attach;
        driver->detach = (JackDriverDetachFunction) alsa_driver_detach;
	driver->wait = (JackDriverWaitFunction) alsa_driver_wait;
	driver->process = (JackDriverProcessFunction) alsa_driver_process;
	driver->start = (JackDriverStartFunction) alsa_driver_audio_start;
	driver->stop = (JackDriverStopFunction) alsa_driver_audio_stop;

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

	driver->clock_mode = ClockMaster; /* XXX is it? */
	driver->input_monitor_mask = 0;   /* XXX is it? */

	driver->capture_ports = 0;
	driver->playback_ports = 0;

	driver->pfd = 0;
	driver->playback_nfds = 0;
	driver->capture_nfds = 0;

	pthread_mutex_init (&driver->clock_sync_lock, 0);
	driver->clock_sync_listeners = 0;
	
	if ((err = snd_pcm_open (&driver->playback_handle, alsa_device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		jack_error ("ALSA: Cannot open PCM device %s/%s", name, alsa_device);
		free (driver);
		return 0;
	}

	driver->alsa_name = strdup (alsa_device);

	if ((err = snd_pcm_open (&driver->capture_handle, alsa_device, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
		snd_pcm_close (driver->playback_handle);
		jack_error ("ALSA: Cannot open PCM device %s", name);
		free (driver);
		return 0;
	}

	if (alsa_driver_check_card_type (driver)) {
		snd_pcm_close (driver->capture_handle);
		snd_pcm_close (driver->playback_handle);
		free (driver);
		return 0;
	}

	driver->playback_hw_params = 0;
	driver->capture_hw_params = 0;
	driver->playback_sw_params = 0;
	driver->capture_sw_params = 0;

	if ((err = snd_pcm_hw_params_malloc (&driver->playback_hw_params)) < 0) {
		jack_error ("ALSA: could no allocate playback hw params structure");
		alsa_driver_delete (driver);
		return 0;
	}

	if ((err = snd_pcm_hw_params_malloc (&driver->capture_hw_params)) < 0) {
		jack_error ("ALSA: could no allocate capture hw params structure");
		alsa_driver_delete (driver);
		return 0;
	}

	if ((err = snd_pcm_sw_params_malloc (&driver->playback_sw_params)) < 0) {
		jack_error ("ALSA: could no allocate playback sw params structure");
		alsa_driver_delete (driver);
		return 0;
	}

	if ((err = snd_pcm_sw_params_malloc (&driver->capture_sw_params)) < 0) {
		jack_error ("ALSA: could no allocate capture sw params structure");
		alsa_driver_delete (driver);
		return 0;
	}

	if (alsa_driver_set_parameters (driver, frames_per_cycle, user_nperiods, rate)) {
		alsa_driver_delete (driver);
		return 0;
	}

	if (snd_pcm_link (driver->capture_handle, driver->playback_handle) != 0) {
		driver->capture_and_playback_not_synced = TRUE;
	} else {
		driver->capture_and_playback_not_synced = FALSE;
	}

	alsa_driver_hw_specific (driver, hw_monitoring);

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
	driver->clock_sync_listeners = g_slist_prepend (driver->clock_sync_listeners, csl);
	pthread_mutex_unlock (&driver->clock_sync_lock);
	return csl->id;
}

int
alsa_driver_stop_listening_to_clock_sync_status (alsa_driver_t *driver, int which)

{
	GSList *node;
	int ret = -1;
	pthread_mutex_lock (&driver->clock_sync_lock);
	for (node = driver->clock_sync_listeners; node; node = g_slist_next (node)) {
		if (((ClockSyncListener *) node->data)->id == which) {
			driver->clock_sync_listeners = g_slist_remove_link (driver->clock_sync_listeners, node);
			free (node->data);
			g_slist_free_1 (node);
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
	GSList *node;

	pthread_mutex_lock (&driver->clock_sync_lock);
	for (node = driver->clock_sync_listeners; node; node = g_slist_next (node)) {
		ClockSyncListener *csl = (ClockSyncListener *) node->data;
		csl->function (chn, status, csl->arg);
	}
	pthread_mutex_unlock (&driver->clock_sync_lock);

}

/* PLUGIN INTERFACE */

jack_driver_t *
driver_initialize (va_list ap)
{
	nframes_t srate;
	nframes_t frames_per_interrupt;
	unsigned long user_nperiods;
	char *pcm_name;
	int hw_monitoring;

	pcm_name = va_arg (ap, char *);
	frames_per_interrupt = va_arg (ap, nframes_t);
	user_nperiods = va_arg(ap, unsigned long);
	srate = va_arg (ap, nframes_t);
	hw_monitoring = va_arg (ap, int);

	return alsa_driver_new ("alsa_pcm", pcm_name, frames_per_interrupt, 
				user_nperiods, srate, hw_monitoring);
}

void
driver_finish (jack_driver_t *driver)
{
	alsa_driver_delete ((alsa_driver_t *) driver);
}

