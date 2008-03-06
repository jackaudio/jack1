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
#include <samplerate.h>

typedef struct _net_driver net_driver_t;

struct _net_driver
{
    JACK_DRIVER_NT_DECL

    jack_nframes_t  net_period_up;
    jack_nframes_t  net_period_down;

    jack_nframes_t  sample_rate;
    jack_nframes_t  bitdepth;
    jack_nframes_t  period_size;

    unsigned int    listen_port;

    unsigned int    capture_channels;
    unsigned int    playback_channels;

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
    unsigned int mtu;
    unsigned int latency;
};


#endif /* __JACK_NET_DRIVER_H__ */
