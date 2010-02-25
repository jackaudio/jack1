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

#include <poll.h>
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


static void
set_period_size (sun_driver_t *driver, jack_nframes_t new_period_size)
{
	driver->period_size = new_period_size;

	driver->period_usecs = 
		((double) driver->period_size /
		(double) driver->sample_rate) * 1e6;
	driver->last_wait_ust = 0;
	driver->iodelay = 0.0F;
	driver->poll_timeout = (int)(driver->period_usecs / 666);
}


static void
sun_driver_write_silence (sun_driver_t *driver, jack_nframes_t nframes)
{
	size_t localsize;
	ssize_t io_res;
	void *localbuf;

	localsize = nframes * driver->sample_bytes * driver->playback_channels;
	localbuf = malloc(localsize);
	if (localbuf == NULL)
	{
		jack_error("sun_driver: malloc() failed: %s@%i",
			__FILE__, __LINE__);
		return;
	}

	bzero(localbuf, localsize);
	io_res = write(driver->outfd, localbuf, localsize);
	if (io_res < (ssize_t) localsize)
	{
		jack_error("sun_driver: write() failed: %s: "
			"count=%d/%d: %s@%i", strerror(errno), io_res,
			localsize, __FILE__, __LINE__);
	}
	free(localbuf);
}


static void
sun_driver_read_silence (sun_driver_t *driver, jack_nframes_t nframes)
{
	size_t localsize;
	ssize_t io_res;
	void *localbuf;

	localsize = nframes * driver->sample_bytes * driver->capture_channels;
	localbuf = malloc(localsize);
	if (localbuf == NULL)
	{
		jack_error("sun_driver: malloc() failed: %s@%i",
			__FILE__, __LINE__);
		return;
	}

	io_res = read(driver->infd, localbuf, localsize);
	if (io_res < (ssize_t) localsize)
	{
		jack_error("sun_driver: read() failed: %s: "
			"count=%d/%d: %s@%i", strerror(errno), io_res,
			localsize, __FILE__, __LINE__);
	}
	free(localbuf);
}


static jack_nframes_t
sun_driver_wait (sun_driver_t *driver, int *status, float *iodelay)
{
	audio_info_t auinfo;
	struct pollfd pfd[2];
	nfds_t nfds;
	float delay;
	jack_time_t poll_enter;
	jack_time_t poll_ret;
	int need_capture = 0;
	int need_playback = 0;
	int capture_errors = 0;
	int playback_errors = 0;
	int capture_seek;
	int playback_seek;

	*status = -1;
	*iodelay = 0;

	pfd[0].fd = driver->infd;
	pfd[0].events = POLLIN;
	if (driver->infd >= 0)
		need_capture = 1;

	pfd[1].fd = driver->outfd;
	pfd[1].events = POLLOUT;
	if (driver->outfd >= 0)
		need_playback = 1;

	poll_enter = jack_get_microseconds();
	if (poll_enter > driver->poll_next)
	{
		/* late. don't count as wakeup delay. */
		driver->poll_next = 0;
	}

	while (need_capture || need_playback)
	{
		nfds = poll(pfd, 2, driver->poll_timeout);
		if ( nfds == -1 ||
		    ((pfd[0].revents | pfd[1].revents) &
		     (POLLERR | POLLHUP | POLLNVAL)) )
		{
			jack_error("sun_driver: poll() error: %s: %s@%i",  
				strerror(errno), __FILE__, __LINE__);
			*status = -3;
			return 0;
		}

		if (nfds == 0)
		{
			jack_error("sun_driver: poll() timeout: %s@%i",
				__FILE__, __LINE__);
			*status = -5;
			return 0;
		}

		if (need_capture && (pfd[0].revents & POLLIN))
		{
			need_capture = 0;
			pfd[0].fd = -1;
		}

		if (need_playback && (pfd[1].revents & POLLOUT))
		{
			need_playback = 0;
			pfd[1].fd = -1;
		}
	}

	poll_ret = jack_get_microseconds();

	if (driver->poll_next && poll_ret > driver->poll_next)
		*iodelay = poll_ret - driver->poll_next;

	driver->poll_last = poll_ret;
	driver->poll_next = poll_ret + driver->period_usecs;
	driver->engine->transport_cycle_start(driver->engine, poll_ret);

#if defined(AUDIO_RERROR) && defined(AUDIO_PERROR)

	/* low level error reporting and recovery.  recovery is necessary
	 * when doing both playback and capture and using AUMODE_PLAY,
	 * because we process one period of both playback and capture data
	 * in each cycle, and we wait in each cycle for that to be possible.
	 * for example, playback will continuously underrun if it underruns
	 * and we have to wait for capture data to become available
	 * before we can write enough playback data to catch up.
	 */

	if (driver->infd >= 0)
	{
		if (ioctl(driver->infd, AUDIO_RERROR, &capture_errors) < 0)
		{
			jack_error("sun_driver: AUDIO_RERROR failed: %s: %s@%i",
				strerror(errno), __FILE__, __LINE__);
			return 0;
		}
		capture_errors -= driver->capture_drops;
		driver->capture_drops += capture_errors;
	}
	if (capture_errors > 0)
	{
		delay = (capture_errors * 1000.0) / driver->sample_rate;
		printf("sun_driver: capture xrun of %d frames (%f msec)\n",
			capture_errors, delay);
	}

	if (driver->outfd >= 0)
	{
		if (ioctl(driver->outfd, AUDIO_PERROR, &playback_errors) < 0)
		{
			jack_error("sun_driver: AUDIO_PERROR failed: %s: %s@%i",
				strerror(errno), __FILE__, __LINE__);
			return 0;
		}
		playback_errors -= driver->playback_drops;
		driver->playback_drops += playback_errors;
	}
	if (playback_errors > 0)
	{
		delay = (playback_errors * 1000.0) / driver->sample_rate;
		printf("sun_driver: playback xrun of %d frames (%f msec)\n",
			playback_errors, delay);
	}

	if ((driver->outfd >= 0 && driver->infd >= 0) &&
		(capture_errors || playback_errors))
	{
		if (ioctl(driver->infd, AUDIO_GETINFO, &auinfo) < 0)
		{
			jack_error("sun_driver: AUDIO_GETINFO failed: %s: "
				"%s@%i", strerror(errno), __FILE__, __LINE__);
			return 0;
		}
		capture_seek = auinfo.record.seek;

		if (driver->infd == driver->outfd)
			playback_seek = auinfo.play.seek;
		else
		{
			if (ioctl(driver->outfd, AUDIO_GETINFO, &auinfo) < 0)
			{
				jack_error("sun_driver: AUDIO_GETINFO failed: "
					"%s: %s@%i", strerror(errno),
					__FILE__, __LINE__);
				return 0;
			}
			playback_seek = auinfo.play.seek;
		}

		capture_seek /= driver->capture_channels *
			driver->sample_bytes;
		playback_seek /= driver->playback_channels *
			driver->sample_bytes;

		if (playback_seek == driver->period_size &&
			capture_seek == driver->period_size  &&
			playback_errors)
		{
			/* normally, 1 period in each buffer is exactly
			 * what we want, but if there was an error then
			 * we effectively have 0 periods in the playback
			 * buffer, because the period in the buffer will
			 * be used to catch up to realtime.
			 */
			printf("sun_driver: writing %d frames of silence "
				"to correct I/O sync\n", driver->period_size);
			sun_driver_write_silence(driver, driver->period_size);
		}
		else if (capture_errors && playback_errors)
		{
			/* serious delay.  we've lost the ability to
			 * write capture_errors frames to catch up on
			 * playback.
			 */
			printf("sun_driver: writing %d frames of silence "
				"to correct I/O sync\n", capture_errors);
			sun_driver_write_silence(driver, capture_errors);
		}
	}

#endif  // AUDIO_RERROR && AUDIO_PERROR

	driver->last_wait_ust = poll_ret;

	*status = 0;

	return driver->period_size;
}


static inline int
sun_driver_run_cycle (sun_driver_t *driver)
{
	jack_nframes_t nframes;
	jack_time_t now;
	int wait_status;
	float iodelay;

	nframes = sun_driver_wait (driver, &wait_status, &iodelay);

	if (wait_status < 0)
	{
		switch (wait_status)
		{
		case -3:
			/* poll() error */
			return -1;
		case -5:
			/* poll() timeout */
			now = jack_get_microseconds();
			if (now > driver->poll_next)
			{
				iodelay = now - driver->poll_next;
				driver->poll_next = now + driver->period_usecs;
				driver->engine->delay(driver->engine, iodelay);
				printf("sun_driver: iodelay = %f\n", iodelay);
			}
			break;
		default:
			/* any other fatal error */
			return -1;
		}
	}

	return driver->engine->run_cycle(driver->engine, nframes, iodelay);
}


static void
copy_and_convert_in (jack_sample_t *dst, void *src, 
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


static void
copy_and_convert_out (void *dst, jack_sample_t *src, 
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


/* jack driver interface */


static int
sun_driver_attach (sun_driver_t *driver)
{
	int port_flags;
	int channel;
	char channel_name[64];
	jack_port_t *port;

	if (driver->engine->set_buffer_size(driver->engine, driver->period_size)) {
		jack_error ("sun_driver: cannot set engine buffer size to %d (check MIDI)", driver->period_size);
		return -1;
	}
	driver->engine->set_sample_rate(driver->engine, driver->sample_rate);

	port_flags = JackPortIsOutput|JackPortIsPhysical|JackPortIsTerminal;

	for (channel = 0; channel < driver->capture_channels; channel++)
	{
		snprintf(channel_name, sizeof(channel_name), 
			"capture_%u", channel + 1);
		port = jack_port_register(driver->client, channel_name,
			JACK_DEFAULT_AUDIO_TYPE, port_flags, 0);
		if (port == NULL)
		{
			jack_error("sun_driver: cannot register port for %s: "
				"%s@%i", channel_name, __FILE__, __LINE__);
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
			jack_error("sun_driver: cannot register port for "
				"%s: %s@%i", channel_name, __FILE__, __LINE__);
			break;
		}
		jack_port_set_latency(port,
			driver->period_size + driver->sys_out_latency);
		driver->playback_ports =
			jack_slist_append(driver->playback_ports, port);
	}

	return jack_activate(driver->client);
}


static int
sun_driver_detach (sun_driver_t *driver)
{
	JSList *node;

	if (driver->engine == NULL)
		return 0;

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

	return 0;
}


static int
sun_driver_start (sun_driver_t *driver)
{
	audio_info_t audio_if;

	if (driver->infd >= 0)
	{
#if defined(AUDIO_FLUSH)
		if (ioctl(driver->infd, AUDIO_FLUSH, NULL) < 0)
		{
			jack_error("sun_driver: capture flush failed: %s: "
				"%s@%i", strerror(errno), __FILE__, __LINE__);
			return -1;
		}
#endif
		AUDIO_INITINFO(&audio_if);
		audio_if.record.pause = 1;
		if (driver->outfd == driver->infd)
			audio_if.play.pause = 1;
		if (ioctl(driver->infd, AUDIO_SETINFO, &audio_if) < 0)
		{
			jack_error("sun_driver: pause capture failed: %s: "
				"%s@%i", strerror(errno), __FILE__, __LINE__);
			return -1;
		}
	}
	if ((driver->outfd >= 0) && (driver->outfd != driver->infd))
	{
#if defined(AUDIO_FLUSH)
		if (ioctl(driver->outfd, AUDIO_FLUSH, NULL) < 0)
		{
			jack_error("sun_driver: playback flush failed: %s: "
				"%s@%i", strerror(errno), __FILE__, __LINE__);
			return -1;
		}
#endif
		AUDIO_INITINFO(&audio_if);
		audio_if.play.pause = 1;
		if (ioctl(driver->outfd, AUDIO_SETINFO, &audio_if) < 0)
		{
			jack_error("sun_driver: pause playback failed: %s: "
				"%s@%i", strerror(errno), __FILE__, __LINE__);
			return -1;
		}
	}

	/* AUDIO_FLUSH resets the counters these work with */
	driver->playback_drops = driver->capture_drops = 0;

	if (driver->outfd >= 0)
	{
		/* "prime" the playback buffer.  if we don't do this, we'll
		 * end up underrunning.  it would get really ugly in duplex
		 * mode, for example, where we have to wait for a period to
		 * be available to read before we can write.  also helps to
		 * keep constant latency from the beginning.
		 */
		sun_driver_write_silence(driver,
			driver->nperiods * driver->period_size);
	}

	if (driver->infd >= 0)
	{
		AUDIO_INITINFO(&audio_if);
		audio_if.record.pause = 0;
		if (driver->outfd == driver->infd)
			audio_if.play.pause = 0;
		if (ioctl(driver->infd, AUDIO_SETINFO, &audio_if) < 0)
		{
			jack_error("sun_driver: start capture failed: %s: "
				"%s@%i", strerror(errno), __FILE__, __LINE__);
			return -1;
		}
	}
	if ((driver->outfd >= 0) && (driver->outfd != driver->infd))
	{
		AUDIO_INITINFO(&audio_if);
		audio_if.play.pause = 0;
		if (ioctl(driver->outfd, AUDIO_SETINFO, &audio_if) < 0)
		{
			jack_error("sun_driver: trigger playback failed: %s: "
				"%s@%i", strerror(errno), __FILE__, __LINE__);
			return -1;
		}
	}

	return 0;
}


static int
enc_equal(int a, int b)
{
	if (a == b)
		return 1;

#if defined(AUDIO_ENCODING_SLINEAR)	
#if BYTE_ORDER == LITTLE_ENDIAN
	if ((a == AUDIO_ENCODING_SLINEAR && b == AUDIO_ENCODING_SLINEAR_LE) ||
	    (a == AUDIO_ENCODING_SLINEAR_LE && b == AUDIO_ENCODING_SLINEAR) ||
	    (a == AUDIO_ENCODING_ULINEAR && b == AUDIO_ENCODING_ULINEAR_LE) ||
	    (a == AUDIO_ENCODING_ULINEAR_LE && b == AUDIO_ENCODING_ULINEAR))
		return 1;
#elif BYTE_ORDER == BIG_ENDIAN
	if ((a == AUDIO_ENCODING_SLINEAR && b == AUDIO_ENCODING_SLINEAR_BE) ||
	    (a == AUDIO_ENCODING_SLINEAR_BE && b == AUDIO_ENCODING_SLINEAR) ||
	    (a == AUDIO_ENCODING_ULINEAR && b == AUDIO_ENCODING_ULINEAR_BE) ||
	    (a == AUDIO_ENCODING_ULINEAR_BE && b == AUDIO_ENCODING_ULINEAR))
		return 1;
#endif
#endif  // AUDIO_ENCODING_SLINEAR
	return 0;
}


static int
sun_driver_set_parameters (sun_driver_t *driver)
{
	audio_info_t audio_if_in, audio_if_out;
	int infd = -1;
	int outfd = -1;
	int s = 1;
	unsigned int cap_period = 0, play_period = 0, period_size = 0;
	const char *indev = driver->indev;
	const char *outdev = driver->outdev;

	driver->indevbuf = NULL;
	driver->outdevbuf = NULL;
	driver->sample_bytes = driver->bits / 8;

	if ((strcmp(indev, outdev) == 0) &&
	    ((driver->capture_channels > 0) && (driver->playback_channels > 0)))
	{
		infd = outfd = open(indev, O_RDWR);
		if (infd < 0)
		{
			jack_error("sun_driver: failed to open duplex device "
				"%s: %s: %s@%i", indev, strerror(errno),
				__FILE__, __LINE__);
			return -1;
		}
#if defined(AUDIO_SETFD)
		if (ioctl(infd, AUDIO_SETFD, &s) < 0)
		{
			jack_error("sun_driver: failed to enable full duplex: "
				"%s: %s@%i", strerror(errno),
				__FILE__, __LINE__);
			return -1;
		}
#endif
	}
	else
	{
		if (driver->capture_channels > 0)
		{
			infd = open(indev, O_RDONLY);
			if (infd < 0)
			{
				jack_error("sun_driver: failed to open input "
					"device %s: %s: %s@%i", indev,
					strerror(errno), __FILE__, __LINE__);
				return -1;
			}
		}
		if (driver->playback_channels > 0)
		{
			outfd = open(outdev, O_WRONLY);
			if (outfd < 0)
			{
				jack_error("sun_driver: failed to open output "
					"device %s: %s: %s@%i", outdev,
					strerror(errno), __FILE__, __LINE__);
				return -1;
			}
		}
	}
	if (infd == -1 && outfd == -1)
	{
		jack_error("sun_driver: no device was opened: %s@%i",
			__FILE__, __LINE__);
		return -1;
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
		audio_if_in.record.pause = 1;
	}
	if (outfd >= 0)
	{
		audio_if_out.play.encoding = driver->format;
		audio_if_out.play.precision = driver->bits;
		audio_if_out.play.channels = driver->playback_channels;
		audio_if_out.play.sample_rate = driver->sample_rate;
		audio_if_out.play.pause = 1;
	}

#if defined(__OpenBSD__) || defined(__NetBSD__)
#if defined(__OpenBSD__)
	if (driver->infd >= 0)
		audio_if_in.record.block_size = driver->capture_channels *
		driver->period_size * driver->sample_bytes;
	if (driver->outfd >= 0)
		audio_if_out.play.block_size = driver->playback_channels *
		driver->period_size * driver->sample_bytes; 
#else
	if (driver->infd >= 0)
		audio_if_in.blocksize = driver->capture_channels *
		driver->period_size * driver->sample_bytes;
	if (driver->outfd >= 0)
		audio_if_out.blocksize =  driver->playback_channels *
		driver->period_size * driver->sample_bytes;
#endif
	if (infd == outfd)
		audio_if_in.play = audio_if_out.play;

	/* this only affects playback.  the capture buffer is
	 * always the max (64k on OpenBSD).
	 */
	audio_if_in.hiwat = audio_if_out.hiwat = driver->nperiods;

	/* AUMODE_PLAY makes us "catch up to realtime" if we underrun
	 * playback.  that means, if we are N frames late, the next
	 * N frames written will be discarded.  this keeps playback
	 * time from expanding with each underrun.
	 */ 
	if (infd == outfd)
	{
		audio_if_in.mode = AUMODE_PLAY | AUMODE_RECORD;
	}
	else
	{
		if (infd >= 0)
			audio_if_in.mode = AUMODE_RECORD;

		if (outfd >= 0)
			audio_if_out.mode = AUMODE_PLAY;
	}

#endif  // OpenBSD || NetBSD

	if (infd >= 0)
	{
		if (ioctl(infd, AUDIO_SETINFO, &audio_if_in) < 0)
		{
			jack_error("sun_driver: failed to set parameters for "
				"%s: %s: %s@%i", indev, strerror(errno),
				__FILE__, __LINE__);
			return -1;
		}
	}

	if (outfd >= 0 && outfd != infd)
	{
		if (ioctl(outfd, AUDIO_SETINFO, &audio_if_out) < 0)
		{
			jack_error("sun_driver: failed to set parameters for "
				"%s: %s: %s@%i", outdev, strerror(errno),
				__FILE__, __LINE__);
			return -1;
		}
	}

	if (infd >= 0)
	{
		if (ioctl(infd, AUDIO_GETINFO, &audio_if_in) < 0)
		{
			jack_error("sun_driver: AUDIO_GETINFO failed: %s: "
				"%s@%i", strerror(errno), __FILE__, __LINE__);
			return -1;
		}

		if (!enc_equal(audio_if_in.record.encoding, driver->format) ||
		    audio_if_in.record.precision != driver->bits ||
		    audio_if_in.record.channels != driver->capture_channels ||
		    audio_if_in.record.sample_rate != driver->sample_rate)
		{
			jack_error("sun_driver: setting capture parameters "
				"failed: %s@%i", __FILE__, __LINE__);
			return -1;
		}
#if defined(__OpenBSD__)
		cap_period = audio_if_in.record.block_size /
			driver->capture_channels / driver->sample_bytes;
#elif defined(__NetBSD__)
		cap_period = audio_if_in.blocksize /
			driver->capture_channels / driver->sample_bytes;
#else
		/* how is this done on Solaris? */
		cap_period = driver->period_size;
#endif
	}

	if (outfd >= 0)
	{
		if (outfd == infd)
		{
			audio_if_out.play = audio_if_in.play;
		}
		else
		{
			if (ioctl(outfd, AUDIO_GETINFO, &audio_if_out) < 0)
			{
				jack_error("sun_driver: AUDIO_GETINFO failed: "
					"%s: %s@%i", strerror(errno),
					__FILE__, __LINE__);
				return -1;
			}
		}

		if (!enc_equal(audio_if_out.play.encoding, driver->format) ||
		    audio_if_out.play.precision != driver->bits ||
		    audio_if_out.play.channels != driver->playback_channels ||
		    audio_if_out.play.sample_rate != driver->sample_rate)
		{
			jack_error("sun_driver: playback settings failed: "
				"%s@%i", __FILE__, __LINE__);
			return -1;
		}
#if defined(__OpenBSD__)
		play_period = audio_if_out.play.block_size /
			driver->playback_channels / driver->sample_bytes;
#elif defined(__NetBSD__)
		play_period = audio_if_out.blocksize /
			driver->playback_channels / driver->sample_bytes;
#else
		/* how is this done on Solaris? */
		play_period = driver->period_size;
#endif
	}

	if (infd >= 0 && outfd >= 0 && play_period != cap_period)
	{
		jack_error("sun_driver: play and capture periods differ: "
		    "%s@%i", __FILE__, __LINE__);
		return -1;
	}
	if (infd >= 0)
		period_size = cap_period;
	else if (outfd >= 0)
		period_size = play_period;

	if (period_size != 0 && period_size != driver->period_size && 
	    !driver->ignorehwbuf)
	{
		printf("sun_driver: period size update: %u\n", period_size);

		set_period_size (driver, period_size);

		if (driver->engine)
			if (driver->engine->set_buffer_size(driver->engine, 
							    driver->period_size)) {
				jack_error ("sun_driver: cannot set engine buffer size to %d (check MIDI)", driver->period_size);
				return -1;
			}
	}

	if (driver->infd >= 0 && driver->capture_channels > 0)
	{
		driver->indevbufsize = driver->period_size * 
			driver->capture_channels * driver->sample_bytes;
		driver->indevbuf = malloc(driver->indevbufsize);
		if (driver->indevbuf == NULL)
		{
			jack_error( "sun_driver: malloc() failed: %s@%i", 
				__FILE__, __LINE__);
			return -1;
		}
		bzero(driver->indevbuf, driver->indevbufsize);
	}
	else
	{
		driver->indevbufsize = 0;
		driver->indevbuf = NULL;
	}

	if (driver->outfd >= 0 && driver->playback_channels > 0)
	{
		driver->outdevbufsize = driver->period_size * 
			driver->playback_channels * driver->sample_bytes;
		driver->outdevbuf = malloc(driver->outdevbufsize);
		if (driver->outdevbuf == NULL)
		{
			jack_error("sun_driver: malloc() failed: %s@%i", 
				__FILE__, __LINE__);
			return -1;
		}
		bzero(driver->outdevbuf, driver->outdevbufsize);
	}
	else
	{
		driver->outdevbufsize = 0;
		driver->outdevbuf = NULL;
	}

	printf("sun_driver: indevbuf %zd B, outdevbuf %zd B\n",
		driver->indevbufsize, driver->outdevbufsize);

	return 0;
}


static int
sun_driver_stop (sun_driver_t *driver)
{
	audio_info_t audio_if;

	if (driver->infd >= 0)
	{
		AUDIO_INITINFO(&audio_if);
		audio_if.record.pause = 1;
		if (driver->outfd == driver->infd)
			audio_if.play.pause = 1;
		if (ioctl(driver->infd, AUDIO_SETINFO, &audio_if) < 0)
		{
			jack_error("sun_driver: capture pause failed: %s: "
				"%s@%i", strerror(errno), __FILE__, __LINE__);
			return -1;
		}
	}

	if ((driver->outfd >= 0) && (driver->outfd != driver->infd))
	{
		AUDIO_INITINFO(&audio_if);
		audio_if.play.pause = 1;
		if (ioctl(driver->outfd, AUDIO_SETINFO, &audio_if) < 0)
		{
			jack_error("sun_driver: playback pause failed: %s: "
				"%s@%i", strerror(errno), __FILE__, __LINE__);
			return -1;
		}
	}

	return 0;
}


static int
sun_driver_read (sun_driver_t *driver, jack_nframes_t nframes)
{
	jack_nframes_t nbytes;
	int channel;
	ssize_t io_res;
	jack_sample_t *portbuf;
	JSList *node;
	jack_port_t *port;

	if (driver->engine->freewheeling || driver->infd < 0)
		return 0;

	if (nframes > driver->period_size)
	{
		jack_error("sun_driver: read failed: nframes > period_size: "
			"(%u/%u): %s@%i", nframes, driver->period_size,
			__FILE__, __LINE__);
		return -1;
	}

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

	nbytes = nframes * driver->capture_channels * driver->sample_bytes;
	io_res = 0;
	while (nbytes)
	{
		io_res = read(driver->infd, driver->indevbuf, nbytes);
		if (io_res < 0)
		{
			jack_error("sun_driver: read() failed: %s: %s@%i",
				strerror(errno), __FILE__, __LINE__);
			break;
		}
		else
			nbytes -= io_res;
	}

	return 0;
}


static int
sun_driver_write (sun_driver_t *driver, jack_nframes_t nframes)
{
	jack_nframes_t nbytes;
	int channel;
	ssize_t io_res;
	jack_sample_t *portbuf;
	JSList *node;
	jack_port_t *port;


	if (driver->engine->freewheeling || driver->outfd < 0)
		return 0;

	if (nframes > driver->period_size)
	{
		jack_error("sun_driver: write failed: nframes > period_size "
			"(%u/%u): %s@%i", nframes, driver->period_size,
			__FILE__, __LINE__);
		return -1;
	}

	bzero(driver->outdevbuf, driver->outdevbufsize);

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

	nbytes = nframes * driver->playback_channels * driver->sample_bytes;
	io_res = 0;
	while (nbytes)
	{
		io_res = write(driver->outfd, driver->outdevbuf, nbytes);
		if (io_res < 0)
		{
			jack_error("sun_driver: write() failed: %s: %s@%i",
				strerror(errno), __FILE__, __LINE__);
			break;
		}
		else
			nbytes -= io_res;
	}

	return 0;
}


static int
sun_driver_null_cycle (sun_driver_t *driver, jack_nframes_t nframes)
{
	if (nframes > driver->period_size)
	{
		jack_error("sun_driver: null cycle failed: "
			"nframes > period_size (%u/%u): %s@%i", nframes,
			driver->period_size, __FILE__, __LINE__);
		return -1;
	}

	printf("sun_driver: running null cycle\n");

	if (driver->outfd >= 0)
		sun_driver_write_silence (driver, nframes);

	if (driver->infd >= 0)
		sun_driver_read_silence (driver, nframes);

	return 0;
}


static int
sun_driver_bufsize (sun_driver_t *driver, jack_nframes_t nframes)
{
	return sun_driver_set_parameters(driver);
}


static void
sun_driver_delete (sun_driver_t *driver)
{
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

	if (driver->indev != NULL)
		free(driver->indev);

	if (driver->outdev != NULL)
		free(driver->outdev);

	jack_driver_nt_finish((jack_driver_nt_t *) driver);

	free(driver);
}


void
driver_finish (jack_driver_t *driver)
{
	sun_driver_delete ((sun_driver_t *)driver);
}


static jack_driver_t *
sun_driver_new (char *indev, char *outdev, jack_client_t *client,
	jack_nframes_t sample_rate, jack_nframes_t period_size,
	jack_nframes_t nperiods, int bits,
	int capture_channels, int playback_channels,
	jack_nframes_t in_latency, jack_nframes_t out_latency,
	int ignorehwbuf)
{
	sun_driver_t *driver;

	driver = (sun_driver_t *) malloc(sizeof(sun_driver_t));
	if (driver == NULL)
	{
		jack_error("sun_driver: malloc() failed: %s: %s@%i",
			strerror(errno), __FILE__, __LINE__);
		return NULL;
	}
	driver->engine = NULL;
	jack_driver_nt_init((jack_driver_nt_t *) driver);

	driver->nt_attach = (JackDriverNTAttachFunction) sun_driver_attach;
	driver->nt_detach = (JackDriverNTDetachFunction) sun_driver_detach;
	driver->read = (JackDriverReadFunction) sun_driver_read;
	driver->write = (JackDriverWriteFunction) sun_driver_write;
	driver->null_cycle = (JackDriverNullCycleFunction) 
		sun_driver_null_cycle;
	driver->nt_bufsize = (JackDriverNTBufSizeFunction) sun_driver_bufsize;
	driver->nt_start = (JackDriverNTStartFunction) sun_driver_start;
	driver->nt_stop = (JackDriverNTStopFunction) sun_driver_stop;
	driver->nt_run_cycle = (JackDriverNTRunCycleFunction) sun_driver_run_cycle;

	if (indev != NULL)
		driver->indev = strdup(indev);
	if (outdev != NULL)
		driver->outdev = strdup(outdev);

	driver->ignorehwbuf = ignorehwbuf;

	driver->sample_rate = sample_rate;
	driver->period_size = period_size;
	driver->nperiods = nperiods;
	driver->bits = bits;
	driver->capture_channels = capture_channels;
	driver->playback_channels = playback_channels;
	driver->sys_in_latency = in_latency;
	driver->sys_out_latency = out_latency;

	set_period_size(driver, period_size);
	
	if (driver->indev == NULL)
		driver->indev = strdup(SUN_DRIVER_DEF_DEV);
	if (driver->outdev == NULL)
		driver->outdev = strdup(SUN_DRIVER_DEF_DEV);
	driver->infd = -1;
	driver->outfd = -1;
#if defined(AUDIO_ENCODING_SLINEAR)
	driver->format = AUDIO_ENCODING_SLINEAR;
#else
	driver->format = AUDIO_ENCODING_LINEAR;
#endif
	driver->indevbuf = driver->outdevbuf = NULL;

	driver->capture_ports = NULL;
	driver->playback_ports = NULL;

	driver->iodelay = 0.0F;
	driver->poll_last = driver->poll_next = 0;

	if (sun_driver_set_parameters (driver) < 0)
	{
		free(driver);
		return NULL;
	}

	driver->client = client;

	return (jack_driver_t *) driver;
}


/* jack driver published interface */


const char driver_client_name[] = "sun";


jack_driver_desc_t *
driver_get_descriptor ()
{
	jack_driver_desc_t *desc;
	jack_driver_param_desc_t *params;

	desc = (jack_driver_desc_t *) calloc(1, sizeof(jack_driver_desc_t));
	if (desc == NULL)
	{
		jack_error("sun_driver: calloc() failed: %s: %s@%i",
			strerror(errno), __FILE__, __LINE__);
		return NULL;
	}
	strcpy(desc->name, driver_client_name);
	desc->nparams = SUN_DRIVER_N_PARAMS;

	params = calloc(desc->nparams, sizeof(jack_driver_param_desc_t));
	if (params == NULL)
	{
		jack_error("sun_driver: calloc() failed: %s: %s@%i",
			strerror(errno), __FILE__, __LINE__);
		return NULL;
	}
	memcpy(params, sun_params, 
		desc->nparams * sizeof(jack_driver_param_desc_t));
	desc->params = params;

	return desc;
}


jack_driver_t *
driver_initialize (jack_client_t *client, JSList * params)
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
	char *indev;
	char *outdev;
	int ignorehwbuf = 0;

	indev = strdup(SUN_DRIVER_DEF_DEV);
	outdev = strdup(SUN_DRIVER_DEF_DEV);

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
				indev = strdup(param->value.str);
				break;
			case 'P':
				outdev = strdup(param->value.str);
				break;
			case 'b':
				ignorehwbuf = 1;
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
	
	return sun_driver_new (indev, outdev, client, sample_rate, period_size,
		nperiods, bits, capture_channels, playback_channels, in_latency,
		out_latency, ignorehwbuf);
}
