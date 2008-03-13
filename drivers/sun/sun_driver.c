/*


	Sun Audio API driver for Jack
	Copyright (C) 2008 Jacob Meuser <jakemsr@sdf.lonestar.org>
	Based heavily on oss_driver.c which came with the following
	copyright notice.

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

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/audioio.h>

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

#include <jack/types.h>
#include <jack/internal.h>
#include <jack/engine.h>
#include <jack/thread.h>
#include <sysdeps/time.h>

#include "sun_driver.h"


#define SUN_DRIVER_N_PARAMS	11
const static jack_driver_param_desc_t sun_params[SUN_DRIVER_N_PARAMS] = {
	{ "rate",
	  'r',
	  JackDriverParamUInt,
	  { .ui = SUN_DRIVER_DEF_FS },
	  "sample rate",
	  "sample rate"
	},
	{ "period",
	  'p',
	  JackDriverParamUInt,
	  { .ui = SUN_DRIVER_DEF_BLKSIZE },
	  "period size",
	  "period size"
	},
	{ "nperiods",
	  'n',
	  JackDriverParamUInt,
	  { .ui = SUN_DRIVER_DEF_NPERIODS },
	  "number of periods in buffer",
	  "number of periods in buffer"
	},
	{ "wordlength",
	  'w',
	  JackDriverParamInt,
	  { .i = SUN_DRIVER_DEF_BITS },
	  "word length",
	  "word length"
	},
	{ "inchannels",
	  'i',
	  JackDriverParamUInt,
	  { .ui = SUN_DRIVER_DEF_INS },
	  "capture channels",
	  "capture channels"
	},
	{ "outchannels",
	  'o',
	  JackDriverParamUInt,
	  { .ui = SUN_DRIVER_DEF_OUTS },
	  "playback channels",
	  "playback channels"
	},
	{ "capture",
	  'C',
	  JackDriverParamString,
	  { .str = SUN_DRIVER_DEF_DEV },
	  "input device",
	  "input device"
	},
	{ "playback",
	  'P',
	  JackDriverParamString,
	  { .str = SUN_DRIVER_DEF_DEV },
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


static void set_period_size (sun_driver_t *driver, 
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


static inline void update_times (sun_driver_t *driver)
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


static inline void driver_cycle (sun_driver_t *driver)
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



static void *io_thread (void *);


/* jack driver interface */


static int sun_driver_attach (sun_driver_t *driver, jack_engine_t *engine)
{
	int port_flags;
	unsigned int channel;
	char channel_name[64];
	jack_port_t *port;

	driver->engine = engine;

	engine->set_buffer_size(engine, driver->period_size);
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
			jack_error("sun_driver: cannot register port for %s: %s@%i",
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
			jack_error("sun_driver: cannot register port for %s: %s@%i",
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


static int sun_driver_detach (sun_driver_t *driver, jack_engine_t *engine)
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


static int sun_driver_start (sun_driver_t *driver)
{
	audio_info_t audio_if_in, audio_if_out;
	int infd = -1;
	int outfd = -1;
	int s = 1;
	unsigned int period_size = 0;
	const char *indev = driver->indev;
	const char *outdev = driver->outdev;

	driver->trigger = 0;

	if ((strcmp(indev, outdev) == 0) &&
	    ((driver->capture_channels > 0) && (driver->playback_channels > 0)))
	{
		infd = outfd = open(indev, O_RDWR|O_EXCL);
		if (infd < 0)
		{
			jack_error(
				"sun_driver: failed to open duplex device %s: %s@%i, errno=%d",
				indev, __FILE__, __LINE__, errno);
			return -1;
		}
		if (ioctl(infd, AUDIO_SETFD, &s) < 0)
		{
			jack_error(
				"sun_driver: failed to enable full duplex for %s: %s@%i, errno=%d",
				indev, __FILE__, __LINE__, errno);
			return -1;
		}
	}
	else
	{
		if (driver->capture_channels > 0)
		{
			infd = open(indev, O_RDONLY|O_EXCL);
			if (infd < 0)
			{
				jack_error(
					"sun_driver: failed to open input device %s: %s@%i, errno=%d",
					indev, __FILE__, __LINE__, errno);
			}
		}
		if (driver->playback_channels > 0)
		{
			outfd = open(outdev, O_WRONLY|O_EXCL);
			if (outfd < 0)
			{
				jack_error(
					"sun_driver: failed to open output device %s: %s@%i, errno=%d",
					outdev, __FILE__, __LINE__, errno);
			}
		}
	}
	if (infd == -1 && outfd == -1)
	{
		jack_error(
			"sun_driver: no device was opened %s@%i", __FILE__, __LINE__);
	}

	driver->infd = infd;
	driver->outfd = outfd;
	
	AUDIO_INITINFO(&audio_if_in);
	AUDIO_INITINFO(&audio_if_out);

	if (infd >= 0)
	{
		audio_if_in.record.encoding = driver->format;
		audio_if_in.record.precision = driver->bits;
		audio_if_in.record.channels = driver->capture_channels;
		audio_if_in.record.sample_rate = driver->sample_rate;

	}
	if (outfd >= 0)
	{
		audio_if_out.play.encoding = driver->format;
		audio_if_out.play.precision = driver->bits;
		audio_if_out.play.channels = driver->playback_channels;
		audio_if_out.play.sample_rate = driver->sample_rate;
	}
	if (infd == outfd)
		audio_if_in.play = audio_if_out.play;

	audio_if_in.hiwat = audio_if_out.hiwat = driver->nperiods;
	audio_if_in.blocksize = driver->period_size * (driver->bits / 8) *
		driver->capture_channels;
	audio_if_out.blocksize =  driver->period_size * (driver->bits / 8) *
		driver->playback_channels;

	if (infd == outfd)
	{
		audio_if_in.mode = AUMODE_PLAY_ALL | AUMODE_RECORD;
	}
	else
	{
		if (infd > 0)
			audio_if_in.mode = AUMODE_RECORD;

		if (outfd > 0)
			audio_if_out.mode = AUMODE_PLAY_ALL;
	}

	if (infd > 0)
	{
		if (ioctl(infd, AUDIO_SETINFO, &audio_if_in) < 0)
			jack_error(
				"sun_driver: failed to set parameters for %s: %s@%i, errno=%d",
				indev, __FILE__, __LINE__, errno);
	}

	if (outfd > 0 && outfd != infd)
	{
		if (ioctl(outfd, AUDIO_SETINFO, &audio_if_out) < 0)
			jack_error(
				"sun_driver: failed to set parameters for %s: %s@%i, errno=%d",
				outdev, __FILE__, __LINE__, errno);
	}

	printf("sun_driver: %s : 0x%x/%i/%i (%i)\n", indev, 
		driver->format, driver->capture_channels, driver->sample_rate,
		driver->period_size);

	if (infd > 0)
	{
		if (ioctl(infd, AUDIO_GETINFO, &audio_if_in) < 0)
		{
			jack_error("sun_driver: AUDIO_GETINFO failed: %s@%i, errno=%d",
				__FILE__, __LINE__, errno);
		}

		if (audio_if_in.record.encoding != driver->format ||
		    audio_if_in.record.precision != driver->bits ||
		    audio_if_in.record.channels != driver->capture_channels ||
		    audio_if_in.record.sample_rate != driver->sample_rate)
		{
			jack_error("sun_driver: setting capture parameters failed: %s@%i",
				__FILE__, __LINE__);
		}

		period_size = 8 * audio_if_in.blocksize / driver->capture_channels /
			driver->bits;
	}

	if (outfd > 0)
	{
		if (outfd == infd)
		{
			audio_if_out.play = audio_if_in.play;
		}
		else
		{
			if (ioctl(outfd, AUDIO_GETINFO, &audio_if_out) < 0)
			{
				jack_error("sun_driver: AUDIO_GETINFO failed: %s@%i, errno=%d",
					__FILE__, __LINE__, errno);
			}
		}

		if (audio_if_in.play.encoding != driver->format ||
		    audio_if_in.play.precision != driver->bits ||
		    audio_if_in.play.channels != driver->playback_channels ||
		    audio_if_in.play.sample_rate != driver->sample_rate)
		{
			jack_error("sun_driver: setting playback parameters failed: %s@%i",
				__FILE__, __LINE__);
		}

		period_size = 8 * audio_if_out.blocksize / driver->playback_channels /
			driver->bits;
	}

	if (period_size != driver->period_size && !driver->ignorehwbuf)
	{
		printf("sun_driver: period size update: %u\n", period_size);
		driver->period_size = period_size;
		driver->period_usecs = ((double) driver->period_size / 
				 (double) driver->sample_rate) * 1e6;

		driver->engine->set_buffer_size(driver->engine, 
			driver->period_size);
	}

	if (driver->capture_channels > 0)
	{
		driver->indevbufsize = driver->period_size * 
			driver->capture_channels * driver->bits / 8;
		driver->indevbuf = malloc(driver->indevbufsize);
		if (driver->indevbuf == NULL)
		{
			jack_error( "sun_driver: malloc() failed: %s@%i", 
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
			driver->playback_channels * driver->bits / 8;
		driver->outdevbuf = malloc(driver->outdevbufsize);
		if (driver->outdevbuf == NULL)
		{
			jack_error("sun_driver: malloc() failed: %s@%i", 
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

	printf("sun_driver: indevbuf %zd B, outdevbuf %zd B\n",
		driver->indevbufsize, driver->outdevbufsize);

	pthread_mutex_init(&driver->mutex_in, NULL);
	pthread_mutex_init(&driver->mutex_out, NULL);
#	ifdef USE_BARRIER
	puts("sun_driver: using barrier mode, (dual thread)");
	pthread_barrier_init(&driver->barrier, NULL, 2);
#	else
	puts("sun_driver: not using barrier mode, (single thread)");
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
			jack_error("sun_driver: jack_client_create_thread() failed: %s@%i",
				__FILE__, __LINE__);
			return -1;
		}
		driver->threads |= 1;
	}

	if ((outfd >= 0) && (infd < 0))
	{
		if (jack_client_create_thread(NULL, &driver->thread_out, 
			driver->engine->rtpriority, 
			driver->engine->control->real_time, 
			io_thread, driver) < 0)
		{
			jack_error("sun_driver: jack_client_create_thread() failed: %s@%i",
				__FILE__, __LINE__);
			return -1;
		}
		driver->threads |= 2;
	}

	if (driver->threads & 1) sem_post(&driver->sem_start);
	if (driver->threads & 2) sem_post(&driver->sem_start);

	driver->last_periodtime = jack_get_microseconds();
	driver->next_periodtime = 0;
	driver->iodelay = 0.0F;

	return 0;
}


static int sun_driver_stop (sun_driver_t *driver)
{
	void *retval;

	driver->run = 0;
	if (driver->threads & 1)
	{
		if (pthread_join(driver->thread_in, &retval) < 0)
		{
			jack_error("sun_driver: pthread_join() failed: %s@%i",
				__FILE__, __LINE__);
			return -1;
		}
	}
	if (driver->threads & 2)
	{
		if (pthread_join(driver->thread_out, &retval) < 0)
		{
			jack_error("sun_driver: pthread_join() failed: %s@%i",
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


static int sun_driver_read (sun_driver_t *driver, jack_nframes_t nframes)
{
	int channel;
	jack_sample_t *portbuf;
	JSList *node;
	jack_port_t *port;

	if (!driver->run) return 0;
	if (nframes != driver->period_size)
	{
		jack_error(
			"sun_driver: read failed nframes != period_size  (%u/%u): %s@%i",
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


static int sun_driver_write (sun_driver_t *driver, jack_nframes_t nframes)
{
	int channel;
	jack_sample_t *portbuf;
	JSList *node;
	jack_port_t *port;

	if (!driver->run) return 0;
	if (nframes != driver->period_size)
	{
		jack_error(
			"sun_driver: write failed nframes != period_size  (%u/%u): %s@%i",
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


static int sun_driver_null_cycle (sun_driver_t *driver, jack_nframes_t nframes)
{
	pthread_mutex_lock(&driver->mutex_in);
	memset(driver->indevbuf, 0x00, driver->indevbufsize);
	pthread_mutex_unlock(&driver->mutex_in);

	pthread_mutex_lock(&driver->mutex_out);
	memset(driver->outdevbuf, 0x00, driver->outdevbufsize);
	pthread_mutex_unlock(&driver->mutex_out);

	return 0;
}


static int sun_driver_bufsize (sun_driver_t *driver, jack_nframes_t nframes)
{
	sun_driver_stop(driver);

	set_period_size(driver, nframes);
	driver->engine->set_buffer_size(driver->engine, driver->period_size);
	printf("sun_driver: period size update: %u\n", nframes);

	sun_driver_start(driver);

	return 0;
}


/* internal driver thread */


#ifdef USE_BARRIER
static inline void synchronize (sun_driver_t *driver)
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
	sun_driver_t *driver = (sun_driver_t *) param;

	sem_wait(&driver->sem_start);

#	ifdef USE_BARRIER
	if (pthread_self() == driver->thread_in)
	{
		localsize = driver->indevbufsize;
		localbuf = malloc(localsize);
		if (localbuf == NULL)
		{
			jack_error("sun_driver: malloc() failed: %s@%i",
				__FILE__, __LINE__);
			return NULL;
		}

		while (driver->run)
		{
			io_res = read(driver->infd, localbuf, localsize);
			if (io_res < (ssize_t) localsize)
			{
				jack_error(
					"sun_driver: read() failed: %s@%i, count=%d/%d, errno=%d",
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
			jack_error("sun_driver: malloc() failed: %s@%i",
				__FILE__, __LINE__);
			return NULL;
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
					"sun_driver: write() failed: %s@%i, count=%d/%d, errno=%d",
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
		jack_error("sun_driver: malloc() failed: %s@%i", __FILE__, __LINE__);
		return NULL;
	}

	while (driver->run)
	{
		if (driver->outfd >= 0 && driver->playback_channels > 0)
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
					"sun_driver: write() failed: %s@%i, count=%d/%d, errno=%d",
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
					"sun_driver: read() failed: %s@%i, count=%d/%d, errno=%d",
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


const char driver_client_name[] = "sun";


void driver_finish (jack_driver_t *);


jack_driver_desc_t * driver_get_descriptor ()
{
	jack_driver_desc_t *desc;
	jack_driver_param_desc_t *params;

	desc = (jack_driver_desc_t *) calloc(1, sizeof(jack_driver_desc_t));
	if (desc == NULL)
	{
		printf("sun_driver: calloc() failed: %s@%i, errno=%d\n",
			__FILE__, __LINE__, errno);
		return NULL;
	}
	strcpy(desc->name, driver_client_name);
	desc->nparams = SUN_DRIVER_N_PARAMS;

	params = calloc(desc->nparams, sizeof(jack_driver_param_desc_t));
	if (params == NULL)
	{
		printf("sun_driver: calloc() failed: %s@%i, errno=%d\n",
			__FILE__, __LINE__, errno);
		return NULL;
	}
	memcpy(params, sun_params, 
		desc->nparams * sizeof(jack_driver_param_desc_t));
	desc->params = params;

	return desc;
}


jack_driver_t * driver_initialize (jack_client_t *client, 
	JSList * params)
{
	int bits = SUN_DRIVER_DEF_BITS;
	jack_nframes_t sample_rate = SUN_DRIVER_DEF_FS;
	jack_nframes_t period_size = SUN_DRIVER_DEF_BLKSIZE;
	jack_nframes_t in_latency = 0;
	jack_nframes_t out_latency = 0;
	unsigned int nperiods = SUN_DRIVER_DEF_NPERIODS;
	unsigned int capture_channels = SUN_DRIVER_DEF_INS;
	unsigned int playback_channels = SUN_DRIVER_DEF_OUTS;
	const JSList *pnode;
	const jack_driver_param_t *param;
	sun_driver_t *driver;

	driver = (sun_driver_t *) malloc(sizeof(sun_driver_t));
	if (driver == NULL)
	{
		jack_error("sun_driver: malloc() failed: %s@%i, errno=%d",
			__FILE__, __LINE__, errno);
		return NULL;
	}
	jack_driver_init((jack_driver_t *) driver);

	driver->attach = (JackDriverAttachFunction) sun_driver_attach;
	driver->detach = (JackDriverDetachFunction) sun_driver_detach;
	driver->start = (JackDriverStartFunction) sun_driver_start;
	driver->stop = (JackDriverStopFunction) sun_driver_stop;
	driver->read = (JackDriverReadFunction) sun_driver_read;
	driver->write = (JackDriverWriteFunction) sun_driver_write;
	driver->null_cycle = (JackDriverNullCycleFunction) 
		sun_driver_null_cycle;
	driver->bufsize = (JackDriverBufSizeFunction) sun_driver_bufsize;

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
		driver->indev = strdup(SUN_DRIVER_DEF_DEV);
	if (driver->outdev == NULL)
		driver->outdev = strdup(SUN_DRIVER_DEF_DEV);
	driver->infd = -1;
	driver->outfd = -1;
	driver->format = AUDIO_ENCODING_SLINEAR_LE;

	driver->indevbuf = driver->outdevbuf = NULL;

	driver->capture_ports = NULL;
	driver->playback_ports = NULL;

	driver->engine = NULL;
	driver->client = client;

	return ((jack_driver_t *) driver);
}


void driver_finish (jack_driver_t *driver)
{
	sun_driver_t *sun_driver = (sun_driver_t *) driver;

	sun_driver = (sun_driver_t *) driver;
	if (sun_driver->indev != NULL)
		free(sun_driver->indev);
	if (sun_driver->outdev != NULL)
		free(sun_driver->outdev);
	free(driver);
}

