
/* -*- mode: c; c-file-style: "linux"; -*- */
/*
NetJack Abstraction.

Copyright (C) 2008 Pieter Palmers <pieterpalmers@users.sourceforge.net>
Copyright (C) 2006 Torben Hohn <torbenh@gmx.de>
Copyright (C) 2003 Robert Ham <rah@bash.sh>
Copyright (C) 2001 Paul Davis

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

$Id: net_driver.c,v 1.17 2006/04/16 20:16:10 torbenh Exp $
*/

#include <math.h>
#include <stdio.h>
#include <memory.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/mman.h>

#include <jack/types.h>
#include <jack/engine.h>
#include <sysdeps/time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "netjack.h"

#include "config.h"
#if HAVE_SAMPLERATE
#include <samplerate.h>
#endif

#if HAVE_CELT
#include <celt/celt.h>
#endif

#include "netjack.h"
#include "netjack_packet.h"

#define MIN(x,y) ((x)<(y) ? (x) : (y))

static int sync_state = TRUE;
static jack_transport_state_t last_transport_state;

static int
net_driver_sync_cb(jack_transport_state_t state, jack_position_t *pos, void *data)
{
    int retval = sync_state;

    if (state == JackTransportStarting && last_transport_state != JackTransportStarting) {
        retval = 0;
    }
//    if (state == JackTransportStarting) 
//		jack_info("Starting sync_state = %d", sync_state);
    last_transport_state = state;
    return retval;
}

void netjack_wait( netjack_driver_state_t *netj )
{
    int we_have_the_expected_frame = 0;
    jack_nframes_t next_frame_avail;
    jack_time_t packet_recv_time_stamp;
    jacknet_packet_header *pkthdr = (jacknet_packet_header *) netj->rx_buf;
    
    if( !netj->next_deadline_valid ) {
	    if( netj->latency == 0 )
		// for full sync mode... always wait for packet.
		netj->next_deadline = jack_get_microseconds() + 500*netj->period_usecs;
	    else if( netj->latency == 1 )
		// for normal 1 period latency mode, only 1 period for dealine.
		netj->next_deadline = jack_get_microseconds() + netj->period_usecs;
	    else
		// looks like waiting 1 period always is correct.
		// not 100% sure yet. with the improved resync, it might be better,
		// to have more than one period headroom for high latency.
		//netj->next_deadline = jack_get_microseconds() + 5*netj->latency*netj->period_usecs/4;
		netj->next_deadline = jack_get_microseconds() + 2*netj->period_usecs;

	    netj->next_deadline_valid = 1;
    } else {
	    netj->next_deadline += netj->period_usecs;
    }

    // Increment expected frame here.
    netj->expected_framecnt += 1;

    // Now check if required packet is already in the cache.
    // then poll (have deadline calculated)
    // then drain socket, rinse and repeat.
    while(1) {
	if( packet_cache_get_next_available_framecnt( global_packcache, netj->expected_framecnt, &next_frame_avail) ) {
	    if( next_frame_avail == netj->expected_framecnt ) {
		we_have_the_expected_frame = 1;
		break;
	    }
	}
	if( ! netjack_poll_deadline( netj->sockfd, netj->next_deadline ) )
	    break;

	packet_cache_drain_socket( global_packcache, netj->sockfd );
    }

    // check if we know who to send our packets too.
    // TODO: there is still something wrong when trying
    // to send back to another port on localhost.
    // need to use -r on netsource for that.
    if (!netj->srcaddress_valid)
	if( global_packcache->master_address_valid ) {
	    memcpy (&(netj->syncsource_address), &(global_packcache->master_address), sizeof( struct sockaddr_in ) );
	    netj->srcaddress_valid = 1;
	}

    // XXX: switching mode unconditionally is stupid.
    //      if we were running free perhaps we like to behave differently
    //      ie. fastforward one packet etc.
    //      well... this is the first packet we see. hmm.... dunno ;S
    //      it works... so...
    netj->running_free = 0;

    if( we_have_the_expected_frame ) {
	netj->time_to_deadline = netj->next_deadline - jack_get_microseconds() - netj->period_usecs;
	packet_cache_retreive_packet( global_packcache, netj->expected_framecnt, (char *) netj->rx_buf, netj->rx_bufsize , &packet_recv_time_stamp);
	//int recv_time_offset = (int) (jack_get_microseconds() - packet_recv_time_stamp);
	packet_header_ntoh(pkthdr);
	netj->deadline_goodness = (int)pkthdr->sync_state;
	netj->packet_data_valid = 1;
	
	// TODO: Queue state could be taken into account.
	//       But needs more processing, cause, when we are running as
	//       fast as we can, recv_time_offset can be zero, which is
	//       good.
	//       need to add (now-deadline) and check that.
	/*
	if( recv_time_offset < netj->period_usecs )
	    //netj->next_deadline -= netj->period_usecs*netj->latency/100;
	    netj->next_deadline += netj->period_usecs/1000;
	    */

	if( netj->deadline_goodness < 10*(int)netj->period_usecs/100*netj->latency ) {
	    netj->next_deadline -= netj->period_usecs/1000;
	    //printf( "goodness: %d, Adjust deadline: --- %d\n", netj->deadline_goodness, (int) netj->period_usecs*netj->latency/100 );
	}
	if( netj->deadline_goodness > 10*(int)netj->period_usecs/100*netj->latency ) {
	    netj->next_deadline += netj->period_usecs/1000;
	    //printf( "goodness: %d, Adjust deadline: +++ %d\n", netj->deadline_goodness, (int) netj->period_usecs*netj->latency/100 );
	}
    } else {
	netj->time_to_deadline = 0;
	// bah... the packet is not there.
	// either 
	// - it got lost.
	// - its late
	// - sync source is not sending anymore.
	
	// lets check if we have the next packets, we will just run a cycle without data.
	// in that case.
	
	if( packet_cache_get_next_available_framecnt( global_packcache, netj->expected_framecnt, &next_frame_avail) ) 
	{
	    jack_nframes_t offset = next_frame_avail - netj->expected_framecnt;

	    //XXX: hmm... i need to remember why resync_threshold wasnt right.
	    //if( offset < netj->resync_threshold )
	    if( offset < 10 ) {
		// ok. dont do nothing. we will run without data. 
		// this seems to be one or 2 lost packets.
		//
		// this can also be reordered packet jitter.
		// (maybe this is not happening in real live)
		//  but it happens in netem.

		netj->packet_data_valid = 0;

		// I also found this happening, when the packet queue, is too full.
		// but wtf ? use a smaller latency. this link can handle that ;S
		if( packet_cache_get_fill( global_packcache, netj->expected_framecnt ) > 80.0 )
		    netj->next_deadline -= netj->period_usecs/2;

		
	    } else {
		// the diff is too high. but we have a packet in the future.
		// lets resync.
		netj->expected_framecnt = next_frame_avail;
		packet_cache_retreive_packet( global_packcache, netj->expected_framecnt, (char *) netj->rx_buf, netj->rx_bufsize, NULL );
		packet_header_ntoh(pkthdr);
		//netj->deadline_goodness = 0;
		netj->deadline_goodness = (int)pkthdr->sync_state - (int)netj->period_usecs * offset;
		netj->next_deadline_valid = 0;
		netj->packet_data_valid = 1;
	    }
	    
	} else {
	    // no packets in buffer.
	    netj->packet_data_valid = 0;
	    
	    //printf( "frame %d No Packet in queue. num_lost_packets = %d \n", netj->expected_framecnt, netj->num_lost_packets ); 
	    if( netj->num_lost_packets < 5 ) {
		// ok. No Packet in queue. The packet was either lost,
		// or we are running too fast.
		//
		// Adjusting the deadline unconditionally resulted in
		// too many xruns on master.
		// But we need to adjust for the case we are running too fast.
		// So lets check if the last packet is there now.
		//
		// It would not be in the queue anymore, if it had been
		// retrieved. This might break for redundancy, but
		// i will make the packet cache drop redundant packets,
		// that have already been retreived.
		//
		if( packet_cache_get_highest_available_framecnt( global_packcache, &next_frame_avail) ) {
		    if( next_frame_avail == (netj->expected_framecnt - 1) ) {
			// Ok. the last packet is there now.
			// and it had not been retrieved.
			// 
			// TODO: We are still dropping 2 packets.
			//       perhaps we can adjust the deadline
			//       when (num_packets lost == 0)
			
			// This might still be too much.
			netj->next_deadline += netj->period_usecs/8;
		    }
		}
	    } else if( (netj->num_lost_packets <= 10) ) { 
		// lets try adjusting the deadline harder, for some packets, we might have just ran 2 fast.
		//netj->next_deadline += netj->period_usecs*netj->latency/8;
	    } else {
		
		// But now we can check for any new frame available.
		//
		if( packet_cache_get_highest_available_framecnt( global_packcache, &next_frame_avail) ) {
		    netj->expected_framecnt = next_frame_avail;
		    packet_cache_retreive_packet( global_packcache, netj->expected_framecnt, (char *) netj->rx_buf, netj->rx_bufsize, NULL );
		    packet_header_ntoh(pkthdr);
		    netj->deadline_goodness = pkthdr->sync_state;
		    netj->next_deadline_valid = 0;
		    netj->packet_data_valid = 1;
		    netj->running_free = 0;
		    printf( "resync after freerun... %d\n", netj->expected_framecnt );
		} else {
		    // give up. lets run freely.
		    // XXX: hmm... 

		    netj->running_free = 1;

		    // when we really dont see packets.
		    // reset source address. and open possibility for new master.
		    // maybe dsl reconnect. Also restart of netsource without fix
		    // reply address changes port.
		    if (netj->num_lost_packets > 200 ) {
			netj->srcaddress_valid = 0;
			packet_cache_reset_master_address( global_packcache );
		    }
		}
	    }
	}
    }

    if( !netj->packet_data_valid )
	netj->num_lost_packets += 1;
    else {
	netj->num_lost_packets = 0;
	//packet_header_ntoh (pkthdr);
    }
}

void netjack_send_silence( netjack_driver_state_t *netj, int syncstate )
{
    int tx_size = get_sample_size(netj->bitdepth) * netj->playback_channels * netj->net_period_up + sizeof(jacknet_packet_header);
    unsigned int *packet_buf, *packet_bufX;

    packet_buf = alloca( tx_size);
    jacknet_packet_header *tx_pkthdr = (jacknet_packet_header *)packet_buf;
    jacknet_packet_header *rx_pkthdr = (jacknet_packet_header *)netj->rx_buf;

    //framecnt = rx_pkthdr->framecnt;

    netj->reply_port = rx_pkthdr->reply_port;

    // offset packet_bufX by the packetheader.
    packet_bufX = packet_buf + sizeof(jacknet_packet_header) / sizeof(jack_default_audio_sample_t);

    tx_pkthdr->sync_state = syncstate;
    tx_pkthdr->framecnt = netj->expected_framecnt;

    // memset 0 the payload.
    int payload_size = get_sample_size(netj->bitdepth) * netj->playback_channels * netj->net_period_up;
    memset(packet_bufX, 0, payload_size);

    packet_header_hton(tx_pkthdr);
    if (netj->srcaddress_valid)
    {
	int r;
	if (netj->reply_port)
	    netj->syncsource_address.sin_port = htons(netj->reply_port);

	for( r=0; r<netj->redundancy; r++ )
	    netjack_sendto(netj->outsockfd, (char *)packet_buf, tx_size,
		    0, (struct sockaddr*)&(netj->syncsource_address), sizeof(struct sockaddr_in), netj->mtu);
    }
}

void netjack_read( netjack_driver_state_t *netj, jack_nframes_t nframes ) 
{
    jack_position_t local_trans_pos;
    jack_transport_state_t local_trans_state;

    unsigned int *packet_buf, *packet_bufX;

    if( ! netj->packet_data_valid ) {
	render_payload_to_jack_ports (netj->bitdepth, NULL, netj->net_period_down, netj->capture_ports, netj->capture_srcs, nframes, netj->dont_htonl_floats );
	return;
    }
    packet_buf = netj->rx_buf;

    jacknet_packet_header *pkthdr = (jacknet_packet_header *)packet_buf;

    packet_bufX = packet_buf + sizeof(jacknet_packet_header) / sizeof(jack_default_audio_sample_t);

    netj->reply_port = pkthdr->reply_port;
    netj->latency = pkthdr->latency;

    // Special handling for latency=0
    if( netj->latency == 0 )
	netj->resync_threshold = 0;
    else
	netj->resync_threshold = MIN( 15, pkthdr->latency-1 );

    // check whether, we should handle the transport sync stuff, or leave trnasports untouched.
    if (netj->handle_transport_sync) {
	int compensated_tranport_pos = (pkthdr->transport_frame + (pkthdr->latency * nframes) + netj->codec_latency);

        // read local transport info....
        local_trans_state = jack_transport_query(netj->client, &local_trans_pos);

        // Now check if we have to start or stop local transport to sync to remote...
        switch (pkthdr->transport_state) {
            case JackTransportStarting:
                // the master transport is starting... so we set our reply to the sync_callback;
                if (local_trans_state == JackTransportStopped) {
                    jack_transport_start(netj->client);
                    last_transport_state = JackTransportStopped;
                    sync_state = FALSE;
                    jack_info("locally stopped... starting...");
                }

                if (local_trans_pos.frame != compensated_tranport_pos)
		{
                    jack_transport_locate(netj->client, compensated_tranport_pos);
                    last_transport_state = JackTransportRolling;
                    sync_state = FALSE;
                    jack_info("starting locate to %d", compensated_tranport_pos );
                }
                break;
            case JackTransportStopped:
                sync_state = TRUE;
                if (local_trans_pos.frame != (pkthdr->transport_frame)) {
                    jack_transport_locate(netj->client, (pkthdr->transport_frame));
                    jack_info("transport is stopped locate to %d", pkthdr->transport_frame);
                }
                if (local_trans_state != JackTransportStopped)
                    jack_transport_stop(netj->client);
                break;
            case JackTransportRolling:
                sync_state = TRUE;
//		    		if(local_trans_pos.frame != (pkthdr->transport_frame + (pkthdr->latency) * nframes)) {
//				    jack_transport_locate(netj->client, (pkthdr->transport_frame + (pkthdr->latency + 2) * nframes));
//				    jack_info("running locate to %d", pkthdr->transport_frame + (pkthdr->latency)*nframes);
//		    		}
                if (local_trans_state != JackTransportRolling)
                    jack_transport_start (netj->client);
                break;

            case JackTransportLooping:
                break;
        }
    }

    render_payload_to_jack_ports (netj->bitdepth, packet_bufX, netj->net_period_down, netj->capture_ports, netj->capture_srcs, nframes, netj->dont_htonl_floats );
}

void netjack_write( netjack_driver_state_t *netj, jack_nframes_t nframes, int syncstate )
{
    uint32_t *packet_buf, *packet_bufX;

    int packet_size = get_sample_size(netj->bitdepth) * netj->playback_channels * netj->net_period_up + sizeof(jacknet_packet_header);
    jacknet_packet_header *pkthdr; 

    packet_buf = alloca(packet_size);
    pkthdr = (jacknet_packet_header *)packet_buf;

    if( netj->running_free ) {
	return;
    }

    // offset packet_bufX by the packetheader.
    packet_bufX = packet_buf + sizeof(jacknet_packet_header) / sizeof(jack_default_audio_sample_t);

    pkthdr->sync_state = syncstate;;
    pkthdr->latency = netj->time_to_deadline;
    //printf( "time to deadline = %d  goodness=%d\n", (int)netj->time_to_deadline, netj->deadline_goodness );
    pkthdr->framecnt = netj->expected_framecnt;


    render_jack_ports_to_payload(netj->bitdepth, netj->playback_ports, netj->playback_srcs, nframes, packet_bufX, netj->net_period_up, netj->dont_htonl_floats );

    packet_header_hton(pkthdr);
    if (netj->srcaddress_valid)
    {
	int r;

#ifdef __APPLE__
	static const int flag = 0;
#else
	static const int flag = MSG_CONFIRM;
#endif

        if (netj->reply_port)
            netj->syncsource_address.sin_port = htons(netj->reply_port);

	for( r=0; r<netj->redundancy; r++ )
	    netjack_sendto(netj->outsockfd, (char *)packet_buf, packet_size,
			   flag, (struct sockaddr*)&(netj->syncsource_address), sizeof(struct sockaddr_in), netj->mtu);
    }
}

void netjack_attach( netjack_driver_state_t *netj )
{
    //puts ("net_driver_attach");
    jack_port_t * port;
    char buf[32];
    unsigned int chn;
    int port_flags;


    if (netj->handle_transport_sync)
        jack_set_sync_callback(netj->client, (JackSyncCallback) net_driver_sync_cb, NULL);

    port_flags = JackPortIsOutput | JackPortIsPhysical | JackPortIsTerminal;

    for (chn = 0; chn < netj->capture_channels_audio; chn++) {
        snprintf (buf, sizeof(buf) - 1, "capture_%u", chn + 1);

        port = jack_port_register (netj->client, buf,
                                   JACK_DEFAULT_AUDIO_TYPE,
                                   port_flags, 0);
        if (!port) {
            jack_error ("NET: cannot register port for %s", buf);
            break;
        }

        netj->capture_ports =
            jack_slist_append (netj->capture_ports, port);

	if( netj->bitdepth == 1000 ) {
#if HAVE_CELT
	    celt_int32_t lookahead;
	    // XXX: memory leak
	    CELTMode *celt_mode = celt_mode_create( netj->sample_rate, 1, netj->period_size, NULL );
	    celt_mode_info( celt_mode, CELT_GET_LOOKAHEAD, &lookahead );
	    netj->codec_latency = 2*lookahead;

	    netj->capture_srcs = jack_slist_append(netj->capture_srcs, celt_decoder_create( celt_mode ) );
#endif
	} else {
#if HAVE_SAMPLERATE 
	    netj->capture_srcs = jack_slist_append(netj->capture_srcs, src_new(SRC_LINEAR, 1, NULL));
#endif
	}
    }
    for (chn = netj->capture_channels_audio; chn < netj->capture_channels; chn++) {
        snprintf (buf, sizeof(buf) - 1, "capture_%u", chn + 1);

        port = jack_port_register (netj->client, buf,
                                   JACK_DEFAULT_MIDI_TYPE,
                                   port_flags, 0);
        if (!port) {
            jack_error ("NET: cannot register port for %s", buf);
            break;
        }

        netj->capture_ports =
            jack_slist_append (netj->capture_ports, port);
    }

    port_flags = JackPortIsInput | JackPortIsPhysical | JackPortIsTerminal;

    for (chn = 0; chn < netj->playback_channels_audio; chn++) {
        snprintf (buf, sizeof(buf) - 1, "playback_%u", chn + 1);

        port = jack_port_register (netj->client, buf,
                                   JACK_DEFAULT_AUDIO_TYPE,
                                   port_flags, 0);

        if (!port) {
            jack_error ("NET: cannot register port for %s", buf);
            break;
        }

        netj->playback_ports =
            jack_slist_append (netj->playback_ports, port);
	if( netj->bitdepth == 1000 ) {
#if HAVE_CELT
	    // XXX: memory leak
	    CELTMode *celt_mode = celt_mode_create( netj->sample_rate, 1, netj->period_size, NULL );
	    netj->playback_srcs = jack_slist_append(netj->playback_srcs, celt_encoder_create( celt_mode ) );
#endif
	} else {
#if HAVE_SAMPLERATE
	    netj->playback_srcs = jack_slist_append(netj->playback_srcs, src_new(SRC_LINEAR, 1, NULL));
#endif
	}
    }
    for (chn = netj->playback_channels_audio; chn < netj->playback_channels; chn++) {
        snprintf (buf, sizeof(buf) - 1, "playback_%u", chn + 1);

        port = jack_port_register (netj->client, buf,
                                   JACK_DEFAULT_MIDI_TYPE,
                                   port_flags, 0);

        if (!port) {
            jack_error ("NET: cannot register port for %s", buf);
            break;
        }

        netj->playback_ports =
            jack_slist_append (netj->playback_ports, port);
    }

    jack_activate (netj->client);
}


void netjack_detach( netjack_driver_state_t *netj )
{
    JSList * node;


    for (node = netj->capture_ports; node; node = jack_slist_next (node))
        jack_port_unregister (netj->client,
                              ((jack_port_t *) node->data));

    jack_slist_free (netj->capture_ports);
    netj->capture_ports = NULL;

    for (node = netj->playback_ports; node; node = jack_slist_next (node))
        jack_port_unregister (netj->client,
                              ((jack_port_t *) node->data));

    jack_slist_free (netj->playback_ports);
    netj->playback_ports = NULL;
}


netjack_driver_state_t *netjack_init (netjack_driver_state_t *netj,
		jack_client_t * client,
                char *name,
                unsigned int capture_ports,
                unsigned int playback_ports,
                unsigned int capture_ports_midi,
                unsigned int playback_ports_midi,
                jack_nframes_t sample_rate,
                jack_nframes_t period_size,
                unsigned int listen_port,
                unsigned int transport_sync,
                unsigned int resample_factor,
                unsigned int resample_factor_up,
                unsigned int bitdepth,
		unsigned int use_autoconfig,
		unsigned int latency,
		unsigned int redundancy,
		int dont_htonl_floats)
{

    // Fill in netj values.
    // might be subject to autoconfig...
    // so dont calculate anything with them...

    int first_pack_len;
    struct sockaddr_in address;

    netj->sample_rate = sample_rate;
    netj->period_size = period_size;
    netj->dont_htonl_floats = dont_htonl_floats;

    netj->listen_port   = listen_port;

    netj->capture_channels  = capture_ports + capture_ports_midi;
    netj->capture_channels_audio  = capture_ports;
    netj->capture_channels_midi   = capture_ports_midi;
    netj->capture_ports     = NULL;
    netj->playback_channels = playback_ports + playback_ports_midi;
    netj->playback_channels_audio = playback_ports;
    netj->playback_channels_midi = playback_ports_midi;
    netj->playback_ports    = NULL;
    netj->codec_latency = 0;

    netj->handle_transport_sync = transport_sync;
    netj->mtu = 1400;
    netj->latency = latency;
    netj->redundancy = redundancy;


    netj->client = client;


    if ((bitdepth != 0) && (bitdepth != 8) && (bitdepth != 16) && (bitdepth != 1000))
    {
        jack_info ("Invalid bitdepth: %d (8, 16 or 0 for float) !!!", bitdepth);
        return NULL;
    }
    netj->bitdepth = bitdepth;


    if (resample_factor_up == 0)
        resample_factor_up = resample_factor;

    // Now open the socket, and wait for the first packet to arrive...
    netj->sockfd = socket (PF_INET, SOCK_DGRAM, 0);
    if (netj->sockfd == -1)
    {
        jack_info ("socket error");
        return NULL;
    }
    address.sin_family = AF_INET;
    address.sin_port = htons(netj->listen_port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind (netj->sockfd, (struct sockaddr *) &address, sizeof (address)) < 0)
    {
        jack_info("bind error");
        return NULL;
    }

    netj->outsockfd = socket (PF_INET, SOCK_DGRAM, 0);
    if (netj->outsockfd == -1)
    {
        jack_info ("socket error");
        return NULL;
    }
    netj->srcaddress_valid = 0;

    if (use_autoconfig)
    {
	jacknet_packet_header *first_packet = alloca (sizeof (jacknet_packet_header));
	socklen_t address_size = sizeof (struct sockaddr_in);

	jack_info ("Waiting for an incoming packet !!!");
	jack_info ("*** IMPORTANT *** Dont connect a client to jackd until the driver is attached to a clock source !!!");

	// XXX: netjack_poll polls forever.
	//      thats ok here.
	if (netjack_poll (netj->sockfd, 500))
	    first_pack_len = recvfrom (netj->sockfd, first_packet, sizeof (jacknet_packet_header), 0, (struct sockaddr*) & netj->syncsource_address, &address_size);
	else
	    first_pack_len = 0;

	netj->srcaddress_valid = 1;

	if (first_pack_len == sizeof (jacknet_packet_header))
	{
	    packet_header_ntoh (first_packet);

	    jack_info ("AutoConfig Override !!!");
	    if (netj->sample_rate != first_packet->sample_rate)
	    {
		jack_info ("AutoConfig Override: Master JACK sample rate = %d", first_packet->sample_rate);
		netj->sample_rate = first_packet->sample_rate;
	    }

	    if (netj->period_size != first_packet->period_size)
	    {
		jack_info ("AutoConfig Override: Master JACK period size is %d", first_packet->period_size);
		netj->period_size = first_packet->period_size;
	    }
	    if (netj->capture_channels_audio != first_packet->capture_channels_audio)
	    {
		jack_info ("AutoConfig Override: capture_channels_audio = %d", first_packet->capture_channels_audio);
		netj->capture_channels_audio = first_packet->capture_channels_audio;
	    }
	    if (netj->capture_channels_midi != first_packet->capture_channels_midi)
	    {
		jack_info ("AutoConfig Override: capture_channels_midi = %d", first_packet->capture_channels_midi);
		netj->capture_channels_midi = first_packet->capture_channels_midi;
	    }
	    if (netj->playback_channels_audio != first_packet->playback_channels_audio)
	    {
		jack_info ("AutoConfig Override: playback_channels_audio = %d", first_packet->playback_channels_audio);
		netj->playback_channels_audio = first_packet->playback_channels_audio;
	    }
	    if (netj->playback_channels_midi != first_packet->playback_channels_midi)
	    {
		jack_info ("AutoConfig Override: playback_channels_midi = %d", first_packet->playback_channels_midi);
		netj->playback_channels_midi = first_packet->playback_channels_midi;
	    }

	    netj->mtu = first_packet->mtu;
	    jack_info ("MTU is set to %d bytes", first_packet->mtu);
	    netj->latency = first_packet->latency;
	}
    }
    netj->capture_channels  = netj->capture_channels_audio + netj->capture_channels_midi;
    netj->playback_channels = netj->playback_channels_audio + netj->playback_channels_midi;

    // After possible Autoconfig: do all calculations...
    netj->period_usecs =
        (jack_time_t) floor ((((float) netj->period_size) / (float)netj->sample_rate)
                             * 1000000.0f);

    if( netj->bitdepth == 1000 ) {
	// celt mode. 
	// TODO: this is a hack. But i dont want to change the packet header.
	netj->net_period_down = resample_factor;
	netj->net_period_up = resample_factor_up;
    } else {
	netj->net_period_down = (float) netj->period_size / (float) resample_factor;
	netj->net_period_up = (float) netj->period_size / (float) resample_factor_up;
    }

    netj->rx_bufsize = sizeof (jacknet_packet_header) + netj->net_period_down * netj->capture_channels * get_sample_size (netj->bitdepth);
    netj->rx_buf = malloc (netj->rx_bufsize);
    netj->pkt_buf = malloc (netj->rx_bufsize);
    global_packcache = packet_cache_new (netj->latency + 5, netj->rx_bufsize, netj->mtu);

    netj->expected_framecnt_valid = 0;
    netj->num_lost_packets = 0;
    netj->next_deadline_valid = 0;
    netj->deadline_goodness = 0;
    netj->time_to_deadline = 0;

    // Special handling for latency=0
    if( netj->latency == 0 )
	netj->resync_threshold = 0;
    else
	netj->resync_threshold = MIN( 15, netj->latency-1 );

    netj->running_free = 0;

    return netj;
}
