/* -*- mode: c; c-file-style: "linux"; -*- */
/*
 *   JACK IEC61883 (FireWire audio) driver
 *
 *   Copyright (C) 2003 Robert Ham <rah@bash.sh>
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
 */

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <poll.h>

#include <jack/internal.h>

#include "iec61883_client.h"
#include "iec61883_common.h"


static enum raw1394_iso_disposition iec61883_client_recv (
	raw1394handle_t handle,
	unsigned char *data,
	unsigned int len,
	unsigned char channel,
	unsigned char tag,
	unsigned char sy,
	unsigned int cycle,
	unsigned int dropped);
static enum raw1394_iso_disposition iec61883_client_xmit (
	raw1394handle_t playback_handle,
	unsigned char *data,
	unsigned int *len,
	unsigned char *tag,
	unsigned char *sy,
	int cycle,
	unsigned int dropped);

const sample_t zero_sample = 0.0;


static raw1394handle_t
iec61883_client_open_raw1394 (int port)
{
	raw1394handle_t raw1394_handle;
	int ret;
	int stale_port_info;
  
	/* open the connection to libraw1394 */
	raw1394_handle = raw1394_new_handle ();
	if (!raw1394_handle) {
		if (errno == 0) {
			jack_error ("IEC61883C: this version of libraw1394 is "
				    "incompatible with your kernel");
		} else {
			jack_error ("IEC61883C: could not create libraw1394 "
				    "handle: %s", strerror (errno));
		}

		return NULL;
	}
  
	/* set the port we'll be using */
	stale_port_info = 1;
	do {
		ret = raw1394_get_port_info (raw1394_handle, NULL, 0);
		if (ret <= port) {
			jack_error ("IEC61883C: port %d is not available", port);
			raw1394_destroy_handle (raw1394_handle);
			return NULL;
		}
  
		ret = raw1394_set_port (raw1394_handle, port);
		if (ret == -1) {
			if (errno != ESTALE) {
				jack_error ("IEC61883C: couldn't use port %d: %s",
					    port, strerror (errno));
				raw1394_destroy_handle (raw1394_handle);
				return NULL;
			}
		}
		else {
			stale_port_info = 0;
		}
	} while (stale_port_info);
  
	return raw1394_handle;
}

static iec61883_buf_set_t **
iec61883_client_create_bufs (unsigned int nbufs,
			     jack_nframes_t fifo_size)
{
	iec61883_buf_set_t ** bufs;
	int i;

	bufs = malloc (sizeof (iec61883_buf_set_t *) * nbufs);
	for (i = 0; i < nbufs; i++) {
		bufs[i] = malloc (sizeof (iec61883_buf_set_t));
		bufs[i]->buffer = jack_ringbuffer_create (fifo_size * sizeof (sample_t) + 1);
	}

	return bufs;
}

static int
iec61883_client_buf_index_from_iso (JSList * infos,
				    unsigned char iso_ch) {
	JSList * node;
	iec61883_channel_info_t * cinfo;
	int aud_index = 0;

	for (node = infos; node; node = jack_slist_next (node)) {
		cinfo = (iec61883_channel_info_t *) node->data;

		if (cinfo->iso_ch == iso_ch) {
			return aud_index;
		}

		aud_index += cinfo->naud_chs;
	}

	jack_error ("IEC61883C: programming error: unknown iso channel %d (!!!)",
		    (int) iso_ch);
	return -1;
}

static iec61883_xmit_cb_info_t *
iec61883_xmit_cb_info_new (iec61883_client_t * client,
			   iec61883_channel_info_t * cinfo)
{
	iec61883_xmit_cb_info_t * info;                


	info = malloc (sizeof (iec61883_xmit_cb_info_t));

	info->client = client;
	info->cinfo  = cinfo;
	info->bufs   = client->play_bufs +
		iec61883_client_buf_index_from_iso (client->play_chs, cinfo->iso_ch);

	return info;
}


static int
iec61883_client_attach_recv_callback (iec61883_client_t * client)
{
	iec61883_channel_info_t * cinfo;
	JSList * node;
	unsigned long i;
	int err;

	if (!client->cap_chs)
		return 0;

	raw1394_set_userdata (client->cap_handle, client);
  
	err = raw1394_iso_multichannel_recv_init (
		client->cap_handle, iec61883_client_recv,
		1024, 1024, -1);
  
	if (err) {
		jack_error ("IEC61883C: could not set recieve callback: %s",
			    strerror (errno));
		return err;
	}
                                            
  
	for (node = client->cap_chs, i = 0;
	     node; node = jack_slist_next (node), i++) {
		cinfo = (iec61883_channel_info_t *) node->data;

		err = raw1394_iso_recv_listen_channel (client->cap_handle,
						       cinfo->iso_ch);
		if (err) {
			jack_error ("IEC61883C: could not listen to channel %d: %s",
				    cinfo->iso_ch, strerror (errno));
			return err;
		}
	}
  
	return 0;
}
	

int
iec61883_client_attach_xmit_callback (iec61883_client_t * client)
{
	iec61883_channel_info_t * cinfo;
	JSList * node;
	unsigned long i;
	int err;

	if (!client->play_chs)
		return 0;
  
 	for (i = 0, node = client->play_chs;
	     node; i++, node = jack_slist_next (node)) {

		cinfo = (iec61883_channel_info_t *) node->data;

		raw1394_set_userdata (client->play_handles[i],
				      iec61883_xmit_cb_info_new (client, cinfo));

		err = raw1394_iso_xmit_init (
			client->play_handles[i],
			iec61883_client_xmit,
			client->irq_interval,
			cinfo->naud_chs * sizeof (sample_t) * client->frames_per_packet,
			cinfo->iso_ch,
			client->speed,
			client->irq_interval);

		if (err) {
			jack_error ("IEC61883C: could not set transmit callback "
				    "for channel %d: %s", cinfo->iso_ch,
				    strerror (errno));
			return err;
		}
	}
  
	return 0;
}

iec61883_client_t * iec61883_client_new (
	jack_client_t * jack_client,
	jack_nframes_t period_size,
	jack_nframes_t fifo_size,
	jack_nframes_t sample_rate,
	int port,
	enum raw1394_iso_speed speed,
	int irq_interval,
	JSList * capture_chs,
	JSList * playback_chs)
{
	iec61883_client_t * client;
	raw1394handle_t capture_handle;
	raw1394handle_t * playback_handles;
	int playback_channel_count;
	int i, j, err;
	JSList * node;

	{
		const char * speed_str;

		switch (speed) {
		case RAW1394_ISO_SPEED_100:
			speed_str = "100";
			break;
		case RAW1394_ISO_SPEED_200:
			speed_str = "200";
			break;
		case RAW1394_ISO_SPEED_400:
			speed_str = "400";
			break;
		}

		printf ("Creating IEC61883 client: %d|%s|%d|%"
			PRIu32 "|%" PRIu32 "|%" PRIu32 "|",
			port, speed_str, irq_interval,
			period_size, fifo_size, sample_rate);

		if (capture_chs) {
			iec61883_client_print_iso_ch_info (capture_chs, stdout);
			putchar ('|');
		} else {
			printf ("-|");
		}

		if (playback_chs) {
			iec61883_client_print_iso_ch_info (playback_chs, stdout);
			putchar ('|');
		} else {
			printf ("-|");
		}

		printf ("2501\n");
	}

	playback_channel_count = jack_slist_length (playback_chs);
  
  
	/* create our handles */
	if (capture_chs) {
		capture_handle = iec61883_client_open_raw1394 (port);
		if (!capture_handle) {
			return NULL;
		}
	} else {
		capture_handle = NULL;
	}

	if (playback_chs) {
		playback_handles = malloc (sizeof (raw1394handle_t) * playback_channel_count);
      
		for (i = 0; i < playback_channel_count; i++) {
			playback_handles[i] = iec61883_client_open_raw1394 (port);
			if (!playback_handles[i]) {
				if (capture_handle) {
					raw1394_destroy_handle (capture_handle);
				}
				for (i--; i >= 0; i--) {
					raw1394_destroy_handle (playback_handles[i]);
				}
				free (playback_handles);
				return NULL;
			}
		}
	} else {
		playback_handles = NULL;
	}

  
	client = calloc (1, sizeof (iec61883_client_t));
  
	client->cap_handle = capture_handle;
	client->play_handles = playback_handles;
  
	client->speed       = speed;
	client->period_size = period_size;
	client->fifo_size   = fifo_size;
	client->sample_rate = sample_rate;
	client->jack_client = jack_client;
  
	/* when you send a packet, you can set a flag to say "interrupt me when
	 * you've sent this packet".  the irq interval is the interval between
	 * packets that have this flag set */

	/*  client->irq_interval = (8000 * period_size) / sample_rate;
	    if (period_size % client->irq_interval != 0) {
	    jack_error ("IEC61883C: asynchronous period size!");
	    abort ();
	    } */
	/*  client->frames_per_packet = period_size / client->irq_interval;  */
  
	if (irq_interval == -1) {
		client->irq_interval = period_size / 3;
	} else {
		client->irq_interval = irq_interval;
	}
  
	client->frames_per_packet = sample_rate / 8000;
  
	printf ("%s: irq_interval: %d, frames per packet: %d\n",
		__FUNCTION__, client->irq_interval, client->frames_per_packet);
  
	client->niso_cap  = jack_slist_length (capture_chs);
	client->niso_play = playback_channel_count;

	if (capture_chs) {

		for (node = capture_chs; node; node = jack_slist_next (node)) {
			client->naud_cap
				+= ((iec61883_channel_info_t *) node->data)->naud_chs;
		}
		client->cap_chs = capture_chs;

		client->cap_bufs = iec61883_client_create_bufs (client->naud_cap,
								client->fifo_size);

		err = iec61883_client_attach_recv_callback (client);
		if (err)
			return NULL;
	}
    
	if (playback_chs) {
		iec61883_channel_info_t * cinfo;
		size_t written;

		for (i = 0, node = playback_chs; node; node = jack_slist_next (node), i++) {
			cinfo = (iec61883_channel_info_t *) node->data;
			client->naud_play += cinfo->naud_chs;
		}
		client->play_chs = playback_chs;

		client->play_bufs = iec61883_client_create_bufs (client->naud_play,
								 client->fifo_size);

		for (i = 0; i < client->naud_play; i++) {
			for (j = 0; j < client->period_size; j++) {
				written =
					jack_ringbuffer_write (client->play_bufs[i]->buffer,
							       (char *) &zero_sample,
							       sizeof (sample_t));

				if (written != sizeof (sample_t)) {
					jack_error ("IEC61883C: ringbuffer not big enough "
						    "to hold a period");
					return NULL;
				}
			}
		}

		err = iec61883_client_attach_xmit_callback (client);
		if (err)
			return NULL;
	}

	pthread_mutex_init (&client->run_lock, NULL);
	client->run = TRUE;

	client->nfds = client->niso_play + (client->cap_chs ? 1 : 0);
	client->pfds = malloc (sizeof (struct pollfd) * client->nfds);

	i = 0;
	if (client->play_chs) {
		for (; i < client->niso_play; i++) {
			client->pfds[i].fd = raw1394_get_fd (client->play_handles[i]);
			client->pfds[i].events = POLLIN|POLLPRI;
		}
	}

	if (client->cap_chs) {
		client->pfds[i].fd = raw1394_get_fd (client->cap_handle);
		client->pfds[i].events = POLLIN|POLLPRI;
	}
  
	return client;
}

void
iec61883_client_destroy (iec61883_client_t * client)
{
	free (client);
}

static void
iec61883_client_do_destroy_ports (iec61883_client_t * client,
				  JSList ** ports)
{
	JSList * node;

	for (node = *ports; node; node = jack_slist_next (node)) {
		jack_port_unregister (client->jack_client, (jack_port_t *) node->data);
	}
	jack_slist_free (*ports);
	*ports = NULL;
}

void
iec61883_client_destroy_ports (iec61883_client_t * client)
{
	iec61883_client_do_destroy_ports (client, &client->cap_ports);
	iec61883_client_do_destroy_ports (client, &client->play_ports);
}

static int
iec61883_client_do_create_ports (jack_client_t * jack_client,
				 JSList * chs,
				 const char * prefix,
				 int flags,
				 JSList ** ports_ptr)
{
	JSList * ports = NULL;
	JSList * node;
	jack_port_t * port;
	iec61883_channel_info_t * ch_info;
	int i;
	char port_name[JACK_PORT_NAME_SIZE];

	for (node = chs; node; node = jack_slist_next (node)) {
		ch_info = (iec61883_channel_info_t *) node->data;

		for (i = 0; i < ch_info->naud_chs; i++) {
			sprintf (port_name, "%s_%d_%d",
				 prefix, (int) ch_info->iso_ch, i);

			port = jack_port_register (jack_client, port_name,
						   JACK_DEFAULT_AUDIO_TYPE,
						   flags, 0);


			if (!port) {
				jack_error ("IEC61883: could not register port %s", port_name);

				for (node = ports; node;
				     node = jack_slist_next (node)) {
					jack_port_unregister (jack_client,
							      (jack_port_t *) node->data);
				}
				jack_slist_free (ports);

				return -1;
			}

			ports = jack_slist_append (ports, port);

			jack_error ("IEC61883CM: registered port %s", port_name);
		}
	}

	*ports_ptr = ports;

	return 0;
}

int
iec61883_client_create_ports (iec61883_client_t * client)
{
	int err;

	if (client->cap_chs) {
		err = iec61883_client_do_create_ports (
			client->jack_client,
			client->cap_chs,
			"capture",
			JackPortIsOutput|JackPortIsTerminal|JackPortIsPhysical,
			&client->cap_ports);

		if (err) {
			return -1;
		}
	}

	if (client->play_chs) {
		err = iec61883_client_do_create_ports (
			client->jack_client,
			client->play_chs,
			"playback",
			JackPortIsInput|JackPortIsTerminal|JackPortIsPhysical,
			&client->play_ports);

		if (err) {
			if (client->cap_chs) {
				iec61883_client_do_destroy_ports (client,
								  &client->cap_ports);
			}
			return -1;
		}
	}

	return 0;
}

static int
iec61883_client_do_read_write (
	jack_nframes_t nframes,
	size_t (*rb_read_write) (jack_ringbuffer_t * rb,
				 char * dest,
				 size_t len),
	iec61883_buf_set_t ** buffers,
	JSList * ports,
	const char * error)
{
	JSList * node;
	sample_t * buffer;
	int i;
	size_t buffer_size;
	size_t len;
	int ret = 0;

	buffer_size = sizeof (sample_t) * nframes;

	for (i = 0, node = ports; node;
	     i++, node = jack_slist_next (node)) {

		buffer = (sample_t *)
			jack_port_get_buffer ((jack_port_t *) node->data, nframes);

		len = rb_read_write (buffers[i]->buffer, (char *) buffer, buffer_size);

		if (len != buffer_size) {
			jack_error (error);
			ret = -1;
		}
	}

	return  ret;
}

int
iec61883_client_read  (iec61883_client_t * client, jack_nframes_t nframes)
{
	return iec61883_client_do_read_write (
		nframes,
		jack_ringbuffer_read,
		client->cap_bufs,
		client->cap_ports,
		"IEC61883C: buffer underrun from IEC61883 client");
}

int
iec61883_client_write (iec61883_client_t * client, jack_nframes_t nframes)
{
	return iec61883_client_do_read_write (
		nframes,
		jack_ringbuffer_write,
		client->play_bufs,
		client->play_ports,
		"IEC61883C: buffer overrun to IEC61883 client");
}

static void
iec61883_client_reset_period (iec61883_client_t * client) {
	int i;

	for (i = 0; i < client->naud_cap; i++) {
		client->cap_bufs[i]->frames_left = client->period_size;
	}
	for (i = 0; i < client->naud_play; i++) {
		client->play_bufs[i]->frames_left = client->period_size;
	}
}

static int
iec61883_client_period_complete (iec61883_client_t * client) {
	int i;

	for (i = 0; i < client->naud_cap; i++) {
		if (client->cap_bufs[i]->frames_left != 0) {
			return FALSE;
		}
	}

	for (i = 0; i < client->naud_play; i++) {
		if (client->play_bufs[i]->frames_left != 0) {
			return FALSE;
		}
	}

	return TRUE;
}

static const char *
iec61883_client_desc_from_fd (iec61883_client_t * client, int fd)
{
	static char desc[64];

	if (client->cap_chs) {
		if (fd == raw1394_get_fd (client->cap_handle)) {
			strcpy (desc, "capture handle");
			return desc;
		}
	}

	if (client->play_chs) {
		int i;
		JSList * node;

		for (node = client->play_chs, i = 0;
		     node; node = jack_slist_next (node), i++) {

			if (fd == raw1394_get_fd (client->play_handles[i])) {
				sprintf (desc, "playback handle for iso channel %d",
					 (int) ((iec61883_channel_info_t *) node->data)->iso_ch);
				return desc;
			}

		}
	}

	strcpy (desc, "unknown handle (!!!)");
	return desc;
}

int
iec61883_client_run_cycle (iec61883_client_t * client)
{
	int done = FALSE;
	int err, i;
	int ret = 0;

	iec61883_client_reset_period (client);

	jack_error ("IEC61883C: hello from client cycle");

	while (!done) {
		jack_error ("IEC61883C: polling");
		err = poll (client->pfds, client->nfds, -1);
		jack_error ("IEC61883C: polled");

		if (err == -1) {
			if (errno == EINTR) {
				continue;
			}

			jack_error ("IEC61883C: poll error: %s\n",
				    strerror (errno));
			return -1;
		}


		for (i = 0; i < client->nfds; i++) {
			if (client->pfds[i].revents & POLLERR) {
				jack_error ("IEC61883C: error on fd for %s",
                                            iec61883_client_desc_from_fd (client,
									  client->pfds[i].fd));
			}

			if (client->pfds[i].revents & POLLHUP) {
				jack_error ("IEC61883C: hangup on fd for %s",
                                            iec61883_client_desc_from_fd (client,
									  client->pfds[i].fd));
			}

			if (client->pfds[i].revents & POLLHUP) {
				jack_error ("IEC61883C: invalid fd on %s",
                                            iec61883_client_desc_from_fd (client,
									  client->pfds[i].fd));
			}

/*			if (client->pfds[i].revents & POLLIN ||
			client->pfds[i].revents & POLLPRI) { */

			err = raw1394_loop_iterate (i < client->niso_play
						    ? client->play_handles[i]
						    : client->cap_handle);
			if (err == -1) {
				jack_error ("IEC61883C: possible raw1394 error: %s",
					    strerror (errno));
				ret = -1;
				done = TRUE;
			}
//			}
		}

		if (client->xrun) {
			jack_error ("IEC61883C: xrun");
			client->xrun = FALSE;
		}

		if (iec61883_client_period_complete (client)) {
			done = TRUE;
		}
	}

	return ret;
}

int
iec61883_client_main (iec61883_client_t * client, pthread_t thread)
{
	int ret = 0;

	client->thread = thread;

	pthread_mutex_lock (&client->run_lock);
	client->running = TRUE;

	while (client->run && !ret) {
		pthread_mutex_unlock (&client->run_lock);

		ret = iec61883_client_run_cycle (client);

		pthread_mutex_lock (&client->run_lock);
	}

	client->running = FALSE;
	pthread_mutex_unlock (&client->run_lock);

	return ret;
}

int
iec61883_client_main_stop (iec61883_client_t * client) {
	int err;

	pthread_mutex_lock (&client->run_lock);
	client->run = FALSE;
	pthread_mutex_unlock (&client->run_lock);

	err = pthread_join (client->thread, NULL);
	if (err) {
		jack_error ("IEC61883C: error waiting for client thread: %s",
			    strerror (err));
	}

	return err;
}


int
iec61883_client_start (iec61883_client_t * client)
{
	int err;
  
	if (client->cap_chs) {
		err = raw1394_iso_recv_start (client->cap_handle, -1, -1, 0);
		if (err) {
			jack_error ("IEC61883C: couldn't start recieving: %s\n",
				    strerror (errno));
			return err;
		}
	}
  
	if (client->play_chs) {
		unsigned long i;
      
		for (i = 0; i < client->niso_play; i++) {
			err = raw1394_iso_xmit_start (client->play_handles[i], -1, -1);
			if (err) {
				jack_error ("IEC61883C: couldn't start transmitting: "
					    "%s\n", strerror (errno));
				return err;
			}
		}
	}
    
	return 0;
}

void
iec61883_client_stop (iec61883_client_t * client)
{
	if (client->cap_chs) {
		raw1394_iso_stop (client->cap_handle);
	}
  
	if (client->play_chs) {
		unsigned long i;
      
		for (i = 0; i < client->niso_play; i++) {
			raw1394_iso_stop (client->play_handles[i]);
		}
	}
}

#define MIN(a,b) ( ((a) > (b)) ? (b) : (a) )

static enum raw1394_iso_disposition
iec61883_client_recv (
	raw1394handle_t handle,
	unsigned char *data,
	unsigned int len,
	unsigned char channel,
	unsigned char tag,
	unsigned char sy,
	unsigned int cycle,
	unsigned int dropped)
{
	iec61883_client_t * client;
	iec61883_channel_info_t * cinfo;
	iec61883_buf_set_t ** bufs;
	int i, j;
	JSList * node;
	jack_nframes_t nframes;
	size_t written;

	printf ("%s: len: %d, sizeof (sample_t): %d\n", __FUNCTION__, len, sizeof (sample_t));

	client = (iec61883_client_t *) raw1394_get_userdata (handle);

	for (node = client->cap_chs; node; node = jack_slist_next (node)) {
		cinfo = (iec61883_channel_info_t *) node->data;

		if (cinfo->iso_ch == channel) {
			break;
		} else {
			cinfo = NULL;
		}
	}

	bufs = client->cap_bufs +
		iec61883_client_buf_index_from_iso (client->cap_chs, channel);

	for (i = 0; i < cinfo->naud_chs; i++) {

  	        nframes = MIN (client->frames_per_packet, bufs[i]->frames_left);
		
		for (j = 0; j < nframes; j++) {
			written = jack_ringbuffer_write (bufs[i]->buffer,
							 (char *) data, sizeof (sample_t));

			if (written != sizeof (sample_t)) {
				jack_error ("IEC61883C: buffer overrun; "
					    "iso ch %d, aud ch %s", channel, i);
			}

			data += sizeof (sample_t);
			bufs[i]->frames_left--;
		}
	}
  
	return RAW1394_ISO_OK;
}

static enum raw1394_iso_disposition
iec61883_client_xmit (
	raw1394handle_t playback_handle,
	unsigned char *data,
	unsigned int *len,
	unsigned char *tag,
	unsigned char *sy,
	int cycle,
	unsigned int dropped)
{
	iec61883_xmit_cb_info_t * info;
	int i, j;
	jack_nframes_t nframes;
	size_t read;
	int underrun = FALSE;
  
	info = raw1394_get_userdata (playback_handle);

	*len = 0;
	for (i = 0; i < info->cinfo->naud_chs; i++) {

		nframes = MIN (info->client->frames_per_packet,
			       info->bufs[i]->frames_left);

		for (j = 0; j < nframes; j++) {
			read = jack_ringbuffer_read (info->bufs[i]->buffer,
						     (char *) data, sizeof (sample_t));
          
			if (read != sizeof (sample_t)) {
/*				jack_error ("IEC61883C: buffer underrun on iso ch %d, audio ch %d: "
					    "read %d, wanted %d", info->cinfo->iso_ch, i,
					    read, sizeof (sample_t));*/
				*data = zero_sample;
				underrun = TRUE;
			}

			data += sizeof (sample_t);
			*len += sizeof (sample_t);
			info->bufs[i]->frames_left--;

/*			jack_error ("IEC61883C: iso: %d, aud: %d, frames: %d",
			(int) info->cinfo->iso_ch, (int) i, (int) info->bufs[i]->frames_left); */
		}
	}

	if (underrun) {
		info->client->xrun = TRUE;
	}

	return RAW1394_ISO_OK;
}


/* EOF */

