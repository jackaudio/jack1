/* -*- mode: c; c-file-style: "linux"; -*- */
/*
 *   JACK IEC16883 (FireWire audio) driver
 *
 *   Copyright (C) Robert Ham 2003 (rah@bash.sh)
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *   This has stuff that is common to both the driver and the in-process
 *   client.
 */

#ifndef __JACK_IEC61883_CLIENT_H__
#define __JACK_IEC61883_CLIENT_H__

#include <libraw1394/raw1394.h>

#include <jack/jslist.h>
#include <jack/types.h>
#include <jack/ringbuffer.h>

#include "iec61883_common.h"

typedef struct _iec61883_client        iec61883_client_t;
typedef struct _iec61883_xmit_cb_info  iec61883_xmit_cb_info_t;
typedef struct _iec61883_buf_set       iec61883_buf_set_t;

struct _iec61883_xmit_cb_info
{
	iec61883_client_t *       client;
	iec61883_channel_info_t * cinfo;
	iec61883_buf_set_t **     bufs;
};

struct _iec61883_buf_set
{
	jack_ringbuffer_t * buffer;
	jack_nframes_t      frames_left;
};

struct _iec61883_client
{
	jack_nframes_t              period_size;
	enum raw1394_iso_speed      speed;
	int                         irq_interval;
	int                         frames_per_packet;
	jack_nframes_t              fifo_size;
	jack_nframes_t              sample_rate;

	JSList *                    cap_chs;
	int                         niso_cap;
	int                         naud_cap;
	JSList *                    play_chs;
	int                         niso_play;
	int                         naud_play;

	iec61883_buf_set_t **       cap_bufs;
	iec61883_buf_set_t **       play_bufs;

 	raw1394handle_t             cap_handle;
	raw1394handle_t *           play_handles;
	int                         nfds;
	struct pollfd *             pfds;
	int                         xrun;

        /* these are only used by the ip client */
	pthread_t                   thread;
	pthread_mutex_t             run_lock;
	int                         run;
	int                         running;

	jack_client_t *             jack_client;  
	JSList *                    cap_ports;
	JSList *                    play_ports;
};



iec61883_client_t * iec61883_client_new (
	jack_client_t * jack_client,
	jack_nframes_t period_size,
	jack_nframes_t fifo_size,
	jack_nframes_t sample_rate,
	int port,
	enum raw1394_iso_speed,
	int irq_interval,
	JSList * capture_chs,
	JSList * playback_chs);
void iec61883_client_destroy (iec61883_client_t *);

int  iec61883_client_start (iec61883_client_t * client);
void iec61883_client_stop  (iec61883_client_t * client);

/* this is intended for the driver and doesn't do any
   threading stuff */
int iec61883_client_run_cycle (iec61883_client_t * client);

/* these are intended for the ip client and does threading
   stuff */
int iec61883_client_main      (iec61883_client_t * client, pthread_t thread);
int iec61883_client_main_stop (iec61883_client_t * client);

int  iec61883_client_create_ports  (iec61883_client_t * client);
void iec61883_client_destroy_ports (iec61883_client_t * client);
int  iec61883_client_read  (iec61883_client_t * client, jack_nframes_t nframes);
int  iec61883_client_write (iec61883_client_t * client, jack_nframes_t nframes);

#endif /* __JACK_IEC61883_CLIENT_H__ */


