/*

	OSS driver for Jack
	Copyright (C) 2003-2005 Jussi Laako <jussi@sonarnerd.net>

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


#ifndef __JACK_OSS_DRIVER_H__
#define __JACK_OSS_DRIVER_H__

#include <sys/types.h>
#include <pthread.h>
#include <semaphore.h>

#include <jack/types.h>
#include <jack/jslist.h>
#include <jack/driver.h>
#include <jack/jack.h>


#define OSS_DRIVER_DEF_DEV	"/dev/dsp"
#define OSS_DRIVER_DEF_FS	48000
#define OSS_DRIVER_DEF_BLKSIZE	1024
#define OSS_DRIVER_DEF_NPERIODS	2
#define OSS_DRIVER_DEF_BITS	16
#define OSS_DRIVER_DEF_INS	2
#define OSS_DRIVER_DEF_OUTS	2


typedef jack_default_audio_sample_t jack_sample_t;

typedef struct _oss_driver
{
	JACK_DRIVER_DECL

	jack_nframes_t sample_rate;
	jack_nframes_t period_size;
	unsigned int nperiods;
	int bits;
	unsigned int capture_channels;
	unsigned int playback_channels;

	char *indev;
	char *outdev;
	int infd;
	int outfd;
	int format;
	int ignorehwbuf;

	size_t indevbufsize;
	size_t outdevbufsize;
	size_t portbufsize;
	void *indevbuf;
	void *outdevbuf;

	float iodelay;
	jack_time_t last_periodtime;
	jack_time_t next_periodtime;
	jack_nframes_t sys_in_latency;
	jack_nframes_t sys_out_latency;

	JSList *capture_ports;
	JSList *playback_ports;

	jack_engine_t *engine;
	jack_client_t *client;

	volatile int run;
	volatile int threads;
	pthread_t thread_in;
	pthread_t thread_out;
	pthread_mutex_t mutex_in;
	pthread_mutex_t mutex_out;
#	ifdef USE_BARRIER
	pthread_barrier_t barrier;
#	endif
	sem_t sem_start;
} oss_driver_t;


#endif

