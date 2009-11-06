/*
    Copyright (C) 2003 Robert Ham <rah@bash.sh>

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
*/


#ifndef __JACK_NET_DRIVER_H__
#define __JACK_NET_DRIVER_H__

#include <unistd.h>

#include <jack/types.h>
#include <jack/driver.h>
#include <jack/jack.h>
#include <jack/transport.h>

#include <netinet/in.h>

#define CELT_MODE 1000 // magic bitdepth value

typedef struct _net_driver net_driver_t;

struct _net_driver
{
    JACK_DRIVER_NT_DECL;

    jack_nframes_t  net_period_up;
    jack_nframes_t  net_period_down;

    jack_nframes_t  sample_rate;
    jack_nframes_t  bitdepth;
    jack_nframes_t  period_size;
    int		    dont_htonl_floats;
    int		    always_wait_dedline;

    jack_nframes_t  codec_latency;

    unsigned int    listen_port;

    unsigned int    capture_channels;
    unsigned int    playback_channels;
    unsigned int    capture_channels_audio;
    unsigned int    playback_channels_audio;
    unsigned int    capture_channels_midi;
    unsigned int    playback_channels_midi;

    JSList	    *capture_ports;
    JSList	    *playback_ports;
    JSList	    *playback_srcs;
    JSList	    *capture_srcs;

    jack_client_t   *client;

    int		    sockfd;
    int		    outsockfd;

    struct sockaddr_in syncsource_address;

    int		    reply_port;
    int		    srcaddress_valid;

    int sync_state;
    unsigned int handle_transport_sync;

    unsigned int *rx_buf;
    unsigned int *pkt_buf;
    unsigned int rx_bufsize;
    //unsigned int tx_bufsize;
    unsigned int mtu;
    unsigned int latency;
    unsigned int redundancy;

    jack_nframes_t expected_framecnt;
    int		   expected_framecnt_valid;
    unsigned int   num_lost_packets;
    jack_time_t	   next_deadline;
    int		   next_deadline_valid;
    int		   packet_data_valid;
    int		   resync_threshold;
    int		   running_free;
    int		   deadline_goodness;
    int		   jitter_val;
    jack_time_t	   time_to_deadline;
};

#endif /* __JACK_NET_DRIVER_H__ */
