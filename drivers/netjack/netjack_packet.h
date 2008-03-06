
/*
 * NetJack - Packet Handling functions
 *
 * used by the driver and the jacknet_client
 *
 * Copyright (C) 2006 Torben Hohn <torbenh@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id: net_driver.c,v 1.16 2006/03/20 19:41:37 torbenh Exp $
 *
 */

#ifndef __JACK_NET_PACKET_H__
#define __JACK_NET_PACKET_H__

#include <jack/jack.h>
#include <jack/types.h>
#include <jack/engine.h>

#include <netinet/in.h>
// The Packet Header.

typedef struct _jacknet_packet_header jacknet_packet_header;

struct _jacknet_packet_header
{
    // General AutoConf Data
    jack_nframes_t channels;
    jack_nframes_t period_size;
    jack_nframes_t sample_rate;

    // Transport Sync
    jack_nframes_t sync_state;
    jack_nframes_t transport_frame;
    jack_nframes_t transport_state;

    // Packet loss Detection, and latency reduction
    jack_nframes_t framecnt;
    jack_nframes_t latency;
    jack_nframes_t reply_port;

    jack_nframes_t mtu;
    jack_nframes_t fragment_nr;
};

typedef union _int_float int_float_t;

union _int_float 
{
    uint32_t i;
    float    f;
};

// fragment reorder cache.
typedef struct _cache_packet cache_packet;

struct _cache_packet
{
    int		    valid;
    int		    num_fragments;
    int		    packet_size;
    int		    mtu;
    jack_nframes_t  framecnt;
    char *	    fragment_array;
    char *	    packet_buf;
};

typedef struct _packet_cache packet_cache;

struct _packet_cache
{
    int size;
    cache_packet *packets;
};

extern packet_cache *global_packcache;

// fragment cache function prototypes
packet_cache *packet_cache_new(int num_packets, int pkt_size, int mtu);
void	      packet_cache_free(packet_cache *pkt_cache);

cache_packet *packet_cache_get_packet(packet_cache *pkt_cache, jack_nframes_t framecnt);
cache_packet *packet_cache_get_oldest_packet(packet_cache *pkt_cache);
cache_packet *packet_cache_get_free_packet(packet_cache *pkt_cache);

void	cache_packet_reset(cache_packet *pack);
void	cache_packet_set_framecnt(cache_packet *pack, jack_nframes_t framecnt);
void	cache_packet_add_fragment(cache_packet *pack, char *packet_buf, int rcv_len);
int		cache_packet_is_complete(cache_packet *pack);

// Function Prototypes

void netjack_sendto(int sockfd, char *packet_buf, int pkt_size, int flags, struct sockaddr *addr, int addr_size, int mtu);
int netjack_recvfrom(int sockfd, char *packet_buf, int pkt_size, int flags, struct sockaddr *addr, socklen_t *addr_size, int mtu);
int netjack_recv(int sockfd, char *packet_buf, int pkt_size, int flags, int mtu);

int get_sample_size(int bitdepth);
void packet_header_hton(jacknet_packet_header *pkthdr);

void packet_header_ntoh(jacknet_packet_header *pkthdr);

void render_payload_to_jack_ports(int bitdepth, void *packet_payload, jack_nframes_t net_period_down, JSList *capture_ports, JSList *capture_srcs, jack_nframes_t nframes);

void render_jack_ports_to_payload(int bitdepth, JSList *playback_ports, JSList *playback_srcs, jack_nframes_t nframes, void *packet_payload, jack_nframes_t net_period_up);

#endif

