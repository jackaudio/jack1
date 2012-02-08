/*

	Sun Audio API driver for Jack
	Copyright (C) 2008 Jacob Meuser <jakemsr@sdf.lonestar.org>
	Based heavily on oss_driver.h which came with the following
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


#ifndef __JACK_SUN_DRIVER_H__
#define __JACK_SUN_DRIVER_H__

#include <sys/types.h>
#include <pthread.h>
#include <semaphore.h>

#include <jack/types.h>
#include <jack/jslist.h>
#include <jack/jack.h>

#include "driver.h"

#define SUN_DRIVER_DEF_DEV	"/dev/audio"
#define SUN_DRIVER_DEF_FS	48000
#define SUN_DRIVER_DEF_BLKSIZE	1024
#define SUN_DRIVER_DEF_NPERIODS	2
#define SUN_DRIVER_DEF_BITS	16
#define SUN_DRIVER_DEF_INS	2
#define SUN_DRIVER_DEF_OUTS	2


typedef jack_default_audio_sample_t jack_sample_t;

typedef struct _sun_driver
{
	JACK_DRIVER_NT_DECL

	jack_nframes_t sample_rate;
	jack_nframes_t period_size;
	unsigned int nperiods;
	int bits;
	int sample_bytes;
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

	int poll_timeout;
	jack_time_t poll_last;
	jack_time_t poll_next;
	float iodelay;

	jack_nframes_t sys_in_latency;
	jack_nframes_t sys_out_latency;

	JSList *capture_ports;
	JSList *playback_ports;

	jack_client_t *client;

	int playback_drops;
	int capture_drops;

} sun_driver_t;


#endif

