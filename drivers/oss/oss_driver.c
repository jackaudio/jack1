/*

	OSS driver for Jack
	Copyright (C) 2003-2007 Jussi Laako <jussi@sonarnerd.net>

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
	Foundation, Inc., 59 Temple Place, Suite 330, Boston,
	MA  02111-1307  USA

*/


#include <config.h>

#ifdef USE_BARRIER
/*
 * POSIX conformance level should be globally defined somewhere, possibly
 * in config.h? Otherwise it's pre 1993/09 level, which leaves out significant
 * parts of threading and realtime stuff. Note: most of the parts are still
 * defined as optional by the standard, so OS conformance to this level
 * doesn't necessarily mean everything exists.
 */
#define _XOPEN_SOURCE	600
#endif
#ifndef _REENTRANT
#define _REENTRANT
#endif
#ifndef _THREAD_SAFE
#define _THREAD_SAFE
#endif
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <float.h>
#include <stdarg.h>
#include <getopt.h>
#include <semaphore.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

#include <jack/types.h>
#include <jack/internal.h>
#include <jack/engine.h>
#include <jack/thread.h>
#include <sysdeps/time.h>

#include "oss_driver.h"


#ifndef SNDCTL_DSP_COOKEDMODE
#ifdef _SIOWR
#define SNDCTL_DSP_COOKEDMODE _SIOWR('P', 30, int)
#else  /* _SIOWR */
#warning "Unable to define cooked mode!"
#define OSS_NO_COOKED_MODE
#endif  /* _SIOWR */
#endif  /* SNDCTL_DSP_COOKEDMODE */

#define OSS_DRIVER_N_PARAMS	11
const static jack_driver_param_desc_t oss_params[OSS_DRIVER_N_PARAMS] = {
	{ "rate",
	  'r',
	  JackDriverParamUInt,
	  { .ui = OSS_DRIVER_DEF_FS },
	  "sample rate",
	  "sample rate"
	},
	{ "period",
	  'p',
	  JackDriverParamUInt,
	  { .ui = OSS_DRIVER_DEF_BLKSIZE },
	  "period size",
	  "period size"
	},
	{ "nperiods",
	  'n',
	  JackDriverParamUInt,
	  { .ui = OSS_DRIVER_DEF_NPERIODS },
	  "number of periods in buffer",
	  "number of periods in buffer"
	},
	{ "wordlength",
	  'w',
	  JackDriverParamInt,
	  { .i = OSS_DRIVER_DEF_BITS },
	  "word length",
	  "word length"
	},
	{ "inchannels",
	  'i',
	  JackDriverParamUInt,
	  { .ui = OSS_DRIVER_DEF_INS },
	  "capture channels",
	  "capture channels"
	},
	{ "outchannels",
	  'o',
	  JackDriverParamUInt,
	  { .ui = OSS_DRIVER_DEF_OUTS },
	  "playback channels",
	  "playback channels"
	},
	{ "capture",
	  'C',
	  JackDriverParamString,
	  { .str = OSS_DRIVER_DEF_DEV },
	  "input device",
	  "input device"
	},
	{ "playback",
	  'P',
	  JackDriverParamString,
	  { .str = OSS_DRIVER_DEF_DEV },
	  "output device",
	  "output device"
	},
	{ "ignorehwbuf",
	  'b',
	  JackDriverParamBool,
	  { },
	  "ignore hardware period size",
	  "ignore hardware period size"
	},
	{ "input latency",
	  'I',
	  JackDriverParamUInt,
	  { .ui = 0 },
	  "system input latency",
	  "system input latency"
	},
	{ "output latency",
	  'O',
	  JackDriverParamUInt,
	  { .ui = 0 },
	  "system output latency",
	  "system output latency"
	}
};



/* internal functions */


static void set_period_size (oss_driver_t *driver, 
	jack_nframes_t new_period_size)
{
	driver->period_size = new_period_size;

	driver->period_usecs = 
		((double) driver->period_size /
		(double) driver->sample_rate) * 1e6;
	driver->last_wait_ust = 0;
	driver->last_periodtime = jack_get_microseconds();
	driver->next_periodtime = 0;
	driver->iodelay = 0.0F;
}


static inline void update_times (oss_driver_t *driver)
{
	driver->last_periodtime = jack_get_microseconds();
	if (driver->next_periodtime > 0)
	{
		driver->iodelay = (float)
			((long double) driver->last_periodtime - 
			(long double) driver->next_periodtime);
	}
	else driver->iodelay = 0.0F;
	driver->next_periodtime = 
		driver->last_periodtime +
		driver->period_usecs;
}


static inline void driver_cycle (oss_driver_t *driver)
{
	update_times(driver);
	driver->engine->transport_cycle_start(driver->engine,
		driver->last_periodtime);

	driver->last_wait_ust = driver->last_periodtime;
	driver->engine->run_cycle(driver->engine, 
		driver->period_size, driver->iodelay);
}


static void copy_and_convert_in (jack_sample_t *dst, void *src, 
	size_t nframes,	int channel, int chcount, int bits)
{
	int srcidx;
	int dstidx;
	signed short *s16src = (signed short *) src;
	signed int *s32src = (signed int *) src;
	double *f64src = (double *) src;
	jack_sample_t scale;

	srcidx = channel;
	switch (bits)
	{
		case 16:
			scale = 1.0f / 0x7fff;
			for (dstidx = 0; dstidx < nframes; dstidx++)
			{
				dst[dstidx] = (jack_sample_t) 
					s16src[srcidx] * scale;
				srcidx += chcount;
			}
			break;
		case 24:
			scale = 1.0f / 0x7fffff;
			for (dstidx = 0; dstidx < nframes; dstidx++)
			{
				dst[dstidx] = (jack_sample_t)
					s32src[srcidx] * scale;
				srcidx += chcount;
			}
			break;
		case 32:
			scale = 1.0f / 0x7fffffff;
			for (dstidx = 0; dstidx < nframes; dstidx++)
			{
				dst[dstidx] = (jack_sample_t)
					s32src[srcidx] * scale;
				srcidx += chcount;
			}
			break;
		case 64:
			for (dstidx = 0; dstidx < nframes; dstidx++)
			{
				dst[dstidx] = (jack_sample_t) f64src[srcidx];
				srcidx += chcount;
			}
			break;
	}
}


static void copy_and_convert_out (void *dst, jack_sample_t *src, 
	size_t nframes,	int channel, int chcount, int bits)
{
	int srcidx;
	int dstidx;
	signed short *s16dst = (signed short *) dst;
	signed int *s32dst = (signed int *) dst;
	double *f64dst = (double *) dst;
	jack_sample_t scale;

	dstidx = channel;
	switch (bits)
	{
		case 16:
			scale = 0x7fff;
			for (srcidx = 0; srcidx < nframes; srcidx++)
			{
				s16dst[dstidx] = (signed short)
					(src[srcidx] >= 0.0f) ?
					(src[srcidx] * scale + 0.5f) :
					(src[srcidx] * scale - 0.5f);
				dstidx += chcount;
			}
			break;
		case 24:
			scale = 0x7fffff;
			for (srcidx = 0; srcidx < nframes; srcidx++)
			{
				s32dst[dstidx] = (signed int)
					(src[srcidx] >= 0.0f) ?
					(src[srcidx] * scale + 0.5f) :
					(src[srcidx] * scale - 0.5f);
				dstidx += chcount;
			}
			break;
		case 32:
			scale = 0x7fffffff;
			for (srcidx = 0; srcidx < nframes; srcidx++)
			{
				s32dst[dstidx] = (signed int)
					(src[srcidx] >= 0.0f) ?
					(src[srcidx] * scale + 0.5f) :
					(src[srcidx] * scale - 0.5f);
				dstidx += chcount;
			}
			break;
		case 64:
			for (srcidx = 0; srcidx < nframes; srcidx++)
			{
				f64dst[dstidx] = (double) src[srcidx];
				dstidx += chcount;
			}
			break;
	}
}


static void set_fragment (int fd, size_t fragsize, unsigned int fragcount)
{
	int fragsize_2p;
	int fragments;

	fragsize_2p = (int) (log(fragsize) / log(2.0) + 0.5);
	fragments = ((fragcount << 16) | (fragsize_2p & 0xffff));
	if (ioctl(fd, SNDCTL_DSP_SETFRAGMENT, &fragments) < 0)
	{
		jack_error("OSS: failed to set fragment size: %s@%i, errno=%d",
			__FILE__, __LINE__, errno);
	}
}


static int get_fragment (int fd)
{
	int fragsize;

	if (ioctl(fd, SNDCTL_DSP_GETBLKSIZE, &fragsize) < 0)
	{
		jack_error("OSS: failed to get fragment size: %s@%i, errno=%d",
			__FILE__, __LINE__, errno);
		return 0;
	}
	return fragsize;
}


static void *io_thread (void *);


/* jack driver interface */


static int oss_driver_attach (oss_driver_t *driver, jack_engine_t *engine)
{
	int port_flags;
	unsigned int channel;
	char channel_name[64];
	jack_port_t *port;

	driver->engine = engine;

	if (engine->set_buffer_size(engine, driver->period_size)) {
		jack_error ("OSS: cannot set engine buffer size to %d (check MIDI)", driver->period_size);
		return -1;
	}
	engine->set_sample_rate(engine, driver->sample_rate);

	port_flags = JackPortIsOutput|JackPortIsPhysical|JackPortIsTerminal;
	for (channel = 0; channel < driver->capture_channels; channel++)
	{
		snprintf(channel_name, sizeof(channel_name), 
			"capture_%u", channel + 1);
		port = jack_port_register(driver->client, channel_name,
			JACK_DEFAULT_AUDIO_TYPE, port_flags, 0);
		if (port == NULL)
		{
			jack_error("OSS: cannot register port for %s: %s@%i",
				channel_name, __FILE__, __LINE__);
			break;
		}
		jack_port_set_latency(port,
			driver->period_size + driver->sys_in_latency);
		driver->capture_ports = 
			jack_slist_append(driver->capture_ports, port);
	}

	port_flags = JackPortIsInput|JackPortIsPhysical|JackPortIsTerminal;
	for (channel = 0; channel < driver->playback_channels; channel++)
	{
		snprintf(channel_name, sizeof(channel_name),
			"playback_%u", channel + 1);
		port = jack_port_register(driver->client, channel_name,
			JACK_DEFAULT_AUDIO_TYPE, port_flags, 0);
		if (port == NULL)
		{
			jack_error("OSS: cannot register port for %s: %s@%i",
				channel_name, __FILE__, __LINE__);
			break;
		}
		jack_port_set_latency(port,
			driver->period_size + driver->sys_out_latency);
		driver->playback_ports =
			jack_slist_append(driver->playback_ports, port);
	}

	jack_activate(driver->client);

	return 0;
}


static int oss_driver_detach (oss_driver_t *driver, jack_engine_t *engine)
{
	JSList *node;

	if (driver->engine == NULL)
		return -1;

	/*jack_deactivate(driver->client);*/	/* ? */

	node = driver->capture_ports;
	while (node != NULL)
	{
		jack_port_unregister(driver->client, 
			((jack_port_t *) node->data));
		node = jack_slist_next(node);
	}
	jack_slist_free(driver->capture_ports);
	driver->capture_ports = NULL;

	node = driver->playback_ports;
	while (node != NULL)
	{
		jack_port_unregister(driver->client,
			((jack_port_t *) node->data));
		node = jack_slist_next(node);
	}
	jack_slist_free(driver->playback_ports);
	driver->playback_ports = NULL;

	driver->engine = NULL;

	return 0;
}


static int oss_driver_start (oss_driver_t *driver)
{
	int flags = 0;
	int format;
	int channels;
	int samplerate;
	int infd = driver->infd;
	int outfd = driver->outfd;
	unsigned int period_size;
	size_t samplesize;
	size_t fragsize;
	const char *indev = driver->indev;
	const char *outdev = driver->outdev;

	switch (driver->bits)
	{
		case 24:
		case 32:
			samplesize = sizeof(int);
			break;
		case 64:
			samplesize = sizeof(double);
			break;
		case 16:
		default:
			samplesize = sizeof(short);
			break;
	}
	driver->trigger = 0;
	if (strcmp(indev, outdev) != 0)
	{
		if (driver->capture_channels > 0)
		{
			infd = open(indev, O_RDONLY|O_EXCL);
			if (infd < 0)
			{
				jack_error(
					"OSS: failed to open input device %s: %s@%i, errno=%d",
					indev, __FILE__, __LINE__, errno);
			}
#ifndef OSS_NO_COOKED_MODE
			ioctl(infd, SNDCTL_DSP_COOKEDMODE, &flags);
#endif
			fragsize = driver->period_size * 
				driver->capture_channels * samplesize;
			set_fragment(infd, fragsize, driver->nperiods);
		}
		else infd = -1;

		if (driver->playback_channels > 0)
		{
			outfd = open(outdev, O_WRONLY|O_EXCL);
			if (outfd < 0)
			{
				jack_error(
					"OSS: failed to open output device %s: %s@%i, errno=%d",
					outdev, __FILE__, __LINE__, errno);
			}
#ifndef OSS_NO_COOKED_MODE
			ioctl(outfd, SNDCTL_DSP_COOKEDMODE, &flags);
#endif
			fragsize = driver->period_size * 
				driver->playback_channels * samplesize;
			set_fragment(outfd, fragsize, driver->nperiods);
		}
		else outfd = -1;
	}
	else
	{
		if (driver->capture_channels != 0 &&
			driver->playback_channels == 0)
		{
			infd = open(indev, O_RDWR|O_EXCL);
			outfd = -1;
			if (infd < 0)
			{
				jack_error(
					"OSS: failed to open device %s: %s@%i, errno=%d",
					indev, __FILE__, __LINE__, errno);
				return -1;
			}
#ifndef OSS_NO_COOKED_MODE
			ioctl(infd, SNDCTL_DSP_COOKEDMODE, &flags);
#endif
		}
		else if (driver->capture_channels == 0 &&
			driver->playback_channels != 0)
		{
			infd = -1;
			outfd = open(outdev, O_RDWR|O_EXCL);
			if (outfd < 0)
			{
				jack_error(
					"OSS: failed to open device %s: %s@%i, errno=%d",
					outdev, __FILE__, __LINE__, errno);
				return -1;
			}
#ifndef OSS_NO_COOKED_MODE
			ioctl(outfd, SNDCTL_DSP_COOKEDMODE, &flags);
#endif
		}
		else
		{
			infd = outfd = open(indev, O_RDWR|O_EXCL);
			if (infd < 0)
			{
				jack_error(
					"OSS: failed to open device %s: %s@%i, errno=%d",
					indev, __FILE__, __LINE__, errno);
				return -1;
			}
#ifndef OSS_NO_COOKED_MODE
			ioctl(infd, SNDCTL_DSP_COOKEDMODE, &flags);
#endif
		}
		if (infd >= 0 && outfd >= 0)
		{
			ioctl(outfd, SNDCTL_DSP_SETTRIGGER, &driver->trigger);
			driver->trigger = (PCM_ENABLE_INPUT|PCM_ENABLE_OUTPUT);
			if (ioctl(infd, SNDCTL_DSP_SETDUPLEX, 0) < 0)
			{
				if (errno != EINVAL) /* Dont care */
					jack_error(
						"OSS: failed to enable full duplex for %s: %s@%i, errno=%d",
						indev, __FILE__, __LINE__,
						errno);
			}
		}
		if (infd >= 0)
		{
			fragsize = driver->period_size * 
				driver->capture_channels * samplesize;
			set_fragment(infd, fragsize, driver->nperiods);
		}
		if (outfd >= 0 && infd < 0)
		{
			fragsize = driver->period_size * 
				driver->playback_channels * samplesize;
			set_fragment(outfd, fragsize, driver->nperiods);
		}
	}
	driver->infd = infd;
	driver->outfd = outfd;
	
	if (infd >= 0)
	{
		format = driver->format;
		if (ioctl(infd, SNDCTL_DSP_SETFMT, &format) < 0)
			jack_error(
				"OSS: failed to set format for %s: %s@%i, errno=%d", 
				indev, __FILE__, __LINE__, errno);
		channels = driver->capture_channels;
		if (ioctl(infd, SNDCTL_DSP_CHANNELS, &channels) < 0)
			jack_error(
				"OSS: failed to set channels for %s: %s@%i, errno=%d", 
				indev, __FILE__, __LINE__, errno);
		samplerate = driver->sample_rate;
		if (ioctl(infd, SNDCTL_DSP_SPEED, &samplerate) < 0)
			jack_error(
				"OSS: failed to set samplerate for %s: %s@%i, errno=%d", 
				indev, __FILE__, __LINE__, errno);
		jack_info("oss_driver: %s : 0x%x/%i/%i (%i)", indev, 
			format, channels, samplerate, get_fragment(infd));
		
		period_size = get_fragment(infd) / samplesize / channels;
		if (period_size != driver->period_size && 
			!driver->ignorehwbuf)
		{
			jack_info("oss_driver: period size update: %u",
				period_size);
			driver->period_size = period_size;
			driver->period_usecs = 
				((double) driver->period_size / 
				 (double) driver->sample_rate) * 1e6;
			if (driver->engine->set_buffer_size(driver->engine, 
							    driver->period_size)) {
				jack_error ("OSS: cannot set engine buffer size to %d (check MIDI)", driver->period_size);
				return -1;
			}
		}
	}

	if (outfd >= 0 && infd != outfd)
	{
		format = driver->format;
		if (ioctl(outfd, SNDCTL_DSP_SETFMT, &format) < 0)
			jack_error(
				"OSS: failed to set format for %s: %s@%i, errno=%d", 
				outdev, __FILE__, __LINE__, errno);
		channels = driver->playback_channels;
		if (ioctl(outfd, SNDCTL_DSP_CHANNELS, &channels) < 0)
			jack_error(
				"OSS: failed to set channels for %s: %s@%i, errno=%d", 
				outdev, __FILE__, __LINE__, errno);
		samplerate = driver->sample_rate;
		if (ioctl(outfd, SNDCTL_DSP_SPEED, &samplerate) < 0)
			jack_error(
				"OSS: failed to set samplerate for %s: %s@%i, errno=%d", 
				outdev, __FILE__, __LINE__, errno);
		jack_info("oss_driver: %s : 0x%x/%i/%i (%i)", outdev, 
			format, channels, samplerate, 
			get_fragment(outfd));

		period_size = get_fragment(outfd) / samplesize / channels;
		if (period_size != driver->period_size &&
			!driver->ignorehwbuf)
		{
			jack_info("oss_driver: period size update: %u",
				period_size);
			driver->period_size = period_size;
			driver->period_usecs = 
				((double) driver->period_size / 
				 (double) driver->sample_rate) * 1e6;
			if (driver->engine->set_buffer_size(driver->engine, 
							    driver->period_size)) {
				jack_error ("OSS: cannot set engine buffer size to %d (check MIDI)", driver->period_size);
				return -1;
			}
		}
	}

	if (driver->capture_channels > 0)
	{
		driver->indevbufsize = driver->period_size * 
			driver->capture_channels * samplesize;
		driver->indevbuf = malloc(driver->indevbufsize);
		if (driver->indevbuf == NULL)
		{
			jack_error( "OSS: malloc() failed: %s@%i", 
				__FILE__, __LINE__);
			return -1;
		}
		memset(driver->indevbuf, 0x00, driver->indevbufsize);
	}
	else
	{
		driver->indevbufsize = 0;
		driver->indevbuf = NULL;
	}

	if (driver->playback_channels > 0)
	{
		driver->outdevbufsize = driver->period_size * 
			driver->playback_channels * samplesize;
		driver->outdevbuf = malloc(driver->outdevbufsize);
		if (driver->outdevbuf == NULL)
		{
			jack_error("OSS: malloc() failed: %s@%i", 
				__FILE__, __LINE__);
			return -1;
		}
		memset(driver->outdevbuf, 0x00, driver->outdevbufsize);
	}
	else
	{
		driver->outdevbufsize = 0;
		driver->outdevbuf = NULL;
	}

	jack_info("oss_driver: indevbuf %zd B, outdevbuf %zd B",
		driver->indevbufsize, driver->outdevbufsize);

	pthread_mutex_init(&driver->mutex_in, NULL);
	pthread_mutex_init(&driver->mutex_out, NULL);
#	ifdef USE_BARRIER
	puts("oss_driver: using barrier mode, (dual thread)");
	pthread_barrier_init(&driver->barrier, NULL, 2);
#	else
	puts("oss_driver: not using barrier mode, (single thread)");
#	endif
	sem_init(&driver->sem_start, 0, 0);
	driver->run = 1;
	driver->threads = 0;
	if (infd >= 0)
	{
		if (jack_client_create_thread(NULL, &driver->thread_in, 
			driver->engine->rtpriority, 
			driver->engine->control->real_time, 
			io_thread, driver) < 0)
		{
			jack_error("OSS: jack_client_create_thread() failed: %s@%i",
				__FILE__, __LINE__);
			return -1;
		}
		driver->threads |= 1;
	}
#	ifdef USE_BARRIER
	if (outfd >= 0)
	{
		if (jack_client_create_thread(NULL, &driver->thread_out, 
			driver->engine->rtpriority, 
			driver->engine->control->real_time, 
			io_thread, driver) < 0)
		{
			jack_error("OSS: jack_client_create_thread() failed: %s@%i",
				__FILE__, __LINE__);
			return -1;
		}
		driver->threads |= 2;
	}
#	endif

	if (driver->threads & 1) sem_post(&driver->sem_start);
	if (driver->threads & 2) sem_post(&driver->sem_start);

	driver->last_periodtime = jack_get_microseconds();
	driver->next_periodtime = 0;
	driver->iodelay = 0.0F;

	return 0;
}


static int oss_driver_stop (oss_driver_t *driver)
{
	void *retval;

	driver->run = 0;
	if (driver->threads & 1)
	{
		if (pthread_join(driver->thread_in, &retval) < 0)
		{
			jack_error("OSS: pthread_join() failed: %s@%i",
				__FILE__, __LINE__);
			return -1;
		}
	}
	if (driver->threads & 2)
	{
		if (pthread_join(driver->thread_out, &retval) < 0)
		{
			jack_error("OSS: pthread_join() failed: %s@%i",
				__FILE__, __LINE__);
			return -1;
		}
	}
	sem_destroy(&driver->sem_start);
#	ifdef USE_BARRIER
	pthread_barrier_destroy(&driver->barrier);
#	endif
	pthread_mutex_destroy(&driver->mutex_in);
	pthread_mutex_destroy(&driver->mutex_out);

	if (driver->outfd >= 0 && driver->outfd != driver->infd)
	{
		close(driver->outfd);
		driver->outfd = -1;
	}
	if (driver->infd >= 0)
	{
		close(driver->infd);
		driver->infd = -1;
	}

	if (driver->indevbuf != NULL)
	{
		free(driver->indevbuf);
		driver->indevbuf = NULL;
	}
	if (driver->outdevbuf != NULL)
	{
		free(driver->outdevbuf);
		driver->outdevbuf = NULL;
	}

	return 0;
}


static int oss_driver_read (oss_driver_t *driver, jack_nframes_t nframes)
{
	int channel;
	jack_sample_t *portbuf;
	JSList *node;
	jack_port_t *port;

	if (!driver->run) return 0;
	if (nframes != driver->period_size)
	{
		jack_error(
			"OSS: read failed nframes != period_size  (%u/%u): %s@%i",
			nframes, driver->period_size, __FILE__, __LINE__);
		return -1;
	}

	pthread_mutex_lock(&driver->mutex_in);

	node = driver->capture_ports;
	channel = 0;
	while (node != NULL)
	{
		port = (jack_port_t *) node->data;

		if (jack_port_connected(port))
		{
			portbuf = jack_port_get_buffer(port, nframes);
			copy_and_convert_in(portbuf, driver->indevbuf, 
				nframes, channel, 
				driver->capture_channels,
				driver->bits);
		}

		node = jack_slist_next(node);
		channel++;
	}

	pthread_mutex_unlock(&driver->mutex_in);

	return 0;
}


static int oss_driver_write (oss_driver_t *driver, jack_nframes_t nframes)
{
	int channel;
	jack_sample_t *portbuf;
	JSList *node;
	jack_port_t *port;

	if (!driver->run) return 0;
	if (nframes != driver->period_size)
	{
		jack_error(
			"OSS: write failed nframes != period_size  (%u/%u): %s@%i",
			nframes, driver->period_size, __FILE__, __LINE__);
		return -1;
	}

	pthread_mutex_lock(&driver->mutex_out);

	node = driver->playback_ports;
	channel = 0;
	while (node != NULL)
	{
		port = (jack_port_t *) node->data;

		if (jack_port_connected(port))
		{
			portbuf = jack_port_get_buffer(port, nframes);
			copy_and_convert_out(driver->outdevbuf, portbuf, 
				nframes, channel,
				driver->playback_channels,
				driver->bits);
		}

		node = jack_slist_next(node);
		channel++;
	}

	pthread_mutex_unlock(&driver->mutex_out);

	return 0;
}


static int oss_driver_null_cycle (oss_driver_t *driver, jack_nframes_t nframes)
{
	pthread_mutex_lock(&driver->mutex_in);
	memset(driver->indevbuf, 0x00, driver->indevbufsize);
	pthread_mutex_unlock(&driver->mutex_in);

	pthread_mutex_lock(&driver->mutex_out);
	memset(driver->outdevbuf, 0x00, driver->outdevbufsize);
	pthread_mutex_unlock(&driver->mutex_out);

	return 0;
}


static int oss_driver_bufsize (oss_driver_t *driver, jack_nframes_t nframes)
{
	oss_driver_stop(driver);

	set_period_size(driver, nframes);
	if (driver->engine->set_buffer_size(driver->engine, driver->period_size)) {
		jack_error ("OSS: cannot set engine buffer size to %d (check MIDI)", driver->period_size);
		return -1;
	}
	jack_info("oss_driver: period size update: %u", nframes);

	oss_driver_start(driver);

	return 0;
}


/* internal driver thread */


#ifdef USE_BARRIER
static inline void synchronize (oss_driver_t *driver)
{
	if (driver->threads == 3)
	{
		if (pthread_barrier_wait(&driver->barrier) ==
			PTHREAD_BARRIER_SERIAL_THREAD)
		{
			driver_cycle(driver);
		}
	}
	else
	{
		driver_cycle(driver);
	}
}
#endif


static void *io_thread (void *param)
{
	size_t localsize;
	ssize_t io_res;
	void *localbuf;
	oss_driver_t *driver = (oss_driver_t *) param;

	sem_wait(&driver->sem_start);

#	ifdef USE_BARRIER
	if (pthread_self() == driver->thread_in)
	{
		localsize = driver->indevbufsize;
		localbuf = malloc(localsize);
		if (localbuf == NULL)
		{
			jack_error("OSS: malloc() failed: %s@%i",
				__FILE__, __LINE__);
			return NULL;
		}

		while (driver->run)
		{
			io_res = read(driver->infd, localbuf, localsize);
			if (io_res < (ssize_t) localsize)
			{
				jack_error(
					"OSS: read() failed: %s@%i, count=%d/%d, errno=%d",
					__FILE__, __LINE__, io_res, localsize,
					errno);
				break;
			}

			pthread_mutex_lock(&driver->mutex_in);
			memcpy(driver->indevbuf, localbuf, localsize);
			pthread_mutex_unlock(&driver->mutex_in);

			synchronize(driver);
		}

		free(localbuf);
	}
	else if (pthread_self() == driver->thread_out)
	{
		localsize = driver->outdevbufsize;
		localbuf = malloc(localsize);
		if (localbuf == NULL)
		{
			jack_error("OSS: malloc() failed: %s@%i",
				__FILE__, __LINE__);
			return NULL;
		}
		if (driver->trigger)
		{
			/* don't care too much if this fails */
			memset(localbuf, 0x00, localsize);
			write(driver->outfd, localbuf, localsize);
			ioctl(driver->outfd, SNDCTL_DSP_SETTRIGGER, &driver->trigger);
		}

		while (driver->run)
		{
			pthread_mutex_lock(&driver->mutex_out);
			memcpy(localbuf, driver->outdevbuf, localsize);
			pthread_mutex_unlock(&driver->mutex_out);

			io_res = write(driver->outfd, localbuf, localsize);
			if (io_res < (ssize_t) localsize)
			{
				jack_error(
					"OSS: write() failed: %s@%i, count=%d/%d, errno=%d",
					__FILE__, __LINE__, io_res, localsize,
					errno);
				break;
			}

			synchronize(driver);
		}

		free(localbuf);
	}
#	else
	localsize = (driver->indevbufsize >= driver->outdevbufsize) ?
		driver->indevbufsize : driver->outdevbufsize;
	localbuf = malloc(localsize);
	if (localbuf == NULL)
	{
		jack_error("OSS: malloc() failed: %s@%i", __FILE__, __LINE__);
		return NULL;
	}
	if (driver->trigger)
	{
		/* don't care too much if this fails */
		memset(localbuf, 0x00, localsize);
		write(driver->outfd, localbuf, driver->outdevbufsize);
		ioctl(driver->outfd, SNDCTL_DSP_SETTRIGGER, &driver->trigger);
	}

	while (driver->run)
	{
		if (driver->playback_channels > 0)
		{
			pthread_mutex_lock(&driver->mutex_out);
			memcpy(localbuf, driver->outdevbuf, 
				driver->outdevbufsize);
			pthread_mutex_unlock(&driver->mutex_out);

			io_res = write(driver->outfd, localbuf, 
				driver->outdevbufsize);
			if (io_res < (ssize_t) driver->outdevbufsize)
			{
				jack_error(
					"OSS: write() failed: %s@%i, count=%d/%d, errno=%d",
					__FILE__, __LINE__, io_res,
					driver->outdevbufsize, errno);
				break;
			}
		}

		if (driver->capture_channels > 0)
		{
			io_res = read(driver->infd, localbuf, 
				driver->indevbufsize);
			if (io_res < (ssize_t) driver->indevbufsize)
			{
				jack_error(
					"OSS: read() failed: %s@%i, count=%d/%d, errno=%d",
					__FILE__, __LINE__, io_res,
					driver->indevbufsize, errno);
				break;
			}

			pthread_mutex_lock(&driver->mutex_in);
			memcpy(driver->indevbuf, localbuf, 
				driver->indevbufsize);
			pthread_mutex_unlock(&driver->mutex_in);
		}

		driver_cycle(driver);
	}

	free(localbuf);
#	endif

	return NULL;
}


/* jack driver published interface */


const char driver_client_name[] = "oss";


void driver_finish (jack_driver_t *);


jack_driver_desc_t * driver_get_descriptor ()
{
	jack_driver_desc_t *desc;
	jack_driver_param_desc_t *params;

	desc = (jack_driver_desc_t *) calloc(1, sizeof(jack_driver_desc_t));
	if (desc == NULL)
	{
		jack_error("oss_driver: calloc() failed: %s@%i, errno=%d",
			__FILE__, __LINE__, errno);
		return NULL;
	}
	strcpy(desc->name, driver_client_name);
	desc->nparams = OSS_DRIVER_N_PARAMS;

	params = calloc(desc->nparams, sizeof(jack_driver_param_desc_t));
	if (params == NULL)
	{
		jack_error("oss_driver: calloc() failed: %s@%i, errno=%d",
			__FILE__, __LINE__, errno);
		return NULL;
	}
	memcpy(params, oss_params, 
		desc->nparams * sizeof(jack_driver_param_desc_t));
	desc->params = params;

	return desc;
}


jack_driver_t * driver_initialize (jack_client_t *client, 
	JSList * params)
{
	int bits = OSS_DRIVER_DEF_BITS;
	jack_nframes_t sample_rate = OSS_DRIVER_DEF_FS;
	jack_nframes_t period_size = OSS_DRIVER_DEF_BLKSIZE;
	jack_nframes_t in_latency = 0;
	jack_nframes_t out_latency = 0;
	unsigned int nperiods = OSS_DRIVER_DEF_NPERIODS;
	unsigned int capture_channels = OSS_DRIVER_DEF_INS;
	unsigned int playback_channels = OSS_DRIVER_DEF_OUTS;
	const JSList *pnode;
	const jack_driver_param_t *param;
	oss_driver_t *driver;

	driver = (oss_driver_t *) malloc(sizeof(oss_driver_t));
	if (driver == NULL)
	{
		jack_error("OSS: malloc() failed: %s@%i, errno=%d",
			__FILE__, __LINE__, errno);
		return NULL;
	}
	jack_driver_init((jack_driver_t *) driver);

	driver->attach = (JackDriverAttachFunction) oss_driver_attach;
	driver->detach = (JackDriverDetachFunction) oss_driver_detach;
	driver->start = (JackDriverStartFunction) oss_driver_start;
	driver->stop = (JackDriverStopFunction) oss_driver_stop;
	driver->read = (JackDriverReadFunction) oss_driver_read;
	driver->write = (JackDriverWriteFunction) oss_driver_write;
	driver->null_cycle = (JackDriverNullCycleFunction) 
		oss_driver_null_cycle;
	driver->bufsize = (JackDriverBufSizeFunction) oss_driver_bufsize;

	driver->indev = NULL;
	driver->outdev = NULL;
	driver->ignorehwbuf = 0;
	driver->trigger = 0;

	pnode = params;
	while (pnode != NULL)
	{
		param = (const jack_driver_param_t *) pnode->data;

		switch (param->character)
		{
			case 'r':
				sample_rate = param->value.ui;
				break;
			case 'p':
				period_size = param->value.ui;
				break;
			case 'n':
				nperiods = param->value.ui;
				break;
			case 'w':
				bits = param->value.i;
				break;
			case 'i':
				capture_channels = param->value.ui;
				break;
			case 'o':
				playback_channels = param->value.ui;
				break;
			case 'C':
				driver->indev = strdup(param->value.str);
				break;
			case 'P':
				driver->outdev = strdup(param->value.str);
				break;
			case 'b':
				driver->ignorehwbuf = 1;
				break;
			case 'I':
				in_latency = param->value.ui;
				break;
			case 'O':
				out_latency = param->value.ui;
				break;
		}
		pnode = jack_slist_next(pnode);
	}
	
	driver->sample_rate = sample_rate;
	driver->period_size = period_size;
	driver->nperiods = nperiods;
	driver->bits = bits;
	driver->capture_channels = capture_channels;
	driver->playback_channels = playback_channels;
	driver->sys_in_latency = in_latency;
	driver->sys_out_latency = out_latency;

	set_period_size(driver, period_size);
	
	driver->finish = driver_finish;

	if (driver->indev == NULL)
		driver->indev = strdup(OSS_DRIVER_DEF_DEV);
	if (driver->outdev == NULL)
		driver->outdev = strdup(OSS_DRIVER_DEF_DEV);
	driver->infd = -1;
	driver->outfd = -1;
	switch (driver->bits)
	{
#		ifndef OSS_ENDIAN
#		ifdef __GNUC__
#		if (defined(__i386__) || defined(__alpha__) || defined(__arm__) || defined(__x86_64__))
#		define OSS_LITTLE_ENDIAN 1234
#		define OSS_ENDIAN OSS_LITTLE_ENDIAN
#		else
#		define OSS_BIG_ENDIAN 4321
#		define OSS_ENDIAN OSS_BIG_ENDIAN
#		endif
#		else /* __GNUC__ */
#		if (defined(_AIX) || defined(AIX) || defined(sparc) || defined(__hppa) || defined(PPC) || defined(__powerpc__) && !defined(i386) && !defined(__i386) && !defined(__i386__))
#		define OSS_BIG_ENDIAN 4321
#		define OSS_ENDIAN OSS_BIG_ENDIAN
#		else
#		define OSS_LITTLE_ENDIAN 1234
#		define OSS_ENDIAN OSS_LITTLE_ENDIAN
#		endif
#		endif /* __GNUC__ */
#		endif /* OSS_ENDIAN */
#		if (OSS_ENDIAN == 1234)
		/* little-endian architectures */
		case 24:	/* little-endian LSB aligned 24-bits in 32-bits  integer */
			driver->format = 0x00008000;
			break;
		case 32:	/* little-endian 32-bit integer */
			driver->format = 0x00001000;
			break;
		case 64:	/* native-endian 64-bit float */
			driver->format = 0x00004000;
			break;
		case 16:	/* little-endian 16-bit integer */
		default:
			driver->format = 0x00000010;
			break;
		/* big-endian architectures */
#		else
		case 24:	/* big-endian LSB aligned 24-bits in 32-bits integer */
			break;
			driver->format = 0x00010000;
		case 32:	/* big-endian 32-bit integer */
			driver->format = 0x00002000;
			break;
		case 64:	/* native-endian 64-bit float */
			driver->format = 0x00004000;
			break;
		case 16:	/* big-endian 16-bit integer */
		default:
			driver->format = 0x00000020;
#		endif
	}

	driver->indevbuf = driver->outdevbuf = NULL;

	driver->capture_ports = NULL;
	driver->playback_ports = NULL;

	driver->engine = NULL;
	driver->client = client;

	return ((jack_driver_t *) driver);
}


void driver_finish (jack_driver_t *driver)
{
	oss_driver_t *oss_driver = (oss_driver_t *) driver;

	oss_driver = (oss_driver_t *) driver;
	if (oss_driver->indev != NULL)
		free(oss_driver->indev);
	if (oss_driver->outdev != NULL)
		free(oss_driver->outdev);
	free(driver);
}

