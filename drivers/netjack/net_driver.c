/* -*- mode: c; c-file-style: "linux"; -*- */
/*
NetJack Driver

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

#include <samplerate.h>

#ifdef HAVE_CELT
#include <celt/celt.h>
#endif

#include "net_driver.h"
#include "netjack_packet.h"

#undef DEBUG_WAKEUP

static int sync_state = TRUE;
static jack_transport_state_t last_transport_state;

/* This is set upon reading a packetand will be
 * written into the pkthdr of an outgoing packet */
static int framecnt; 

static int
net_driver_sync_cb(jack_transport_state_t state, jack_position_t *pos, net_driver_t *driver)
{
    int retval = sync_state;

    if (state == JackTransportStarting && last_transport_state != JackTransportStarting) {
        retval = 0;
    }
    if (state == JackTransportStarting) 
		jack_info("Starting sync_state = %d", sync_state);
    last_transport_state = state;
    return retval;
}

static jack_nframes_t
net_driver_wait (net_driver_t *driver, int extra_fd, int *status, float *delayed_usecs)
{
    // ok... we wait for a packet on the socket
    // TODO:
    // we should be able to run freely if the sync source is not transmitting
    // especially we should be able to compensate for packet loss somehow.
    // but lets try this out first.
    //
    // on packet loss we should either detect an xrun or just continue running when we
    // think, that the sync source is not running anymore.

    //socklen_t address_size = sizeof (struct sockaddr_in);
    //int len;
    int we_have_the_expected_frame = 0;
    jack_nframes_t next_frame_avail;
    jacknet_packet_header *pkthdr = (jacknet_packet_header *) driver->rx_buf;
    
    if( !driver->next_deadline_valid ) {
	    driver->next_deadline = jack_get_microseconds() + 2*driver->period_usecs;
	    driver->next_deadline_valid = 1;
    } else {
	    driver->next_deadline += driver->period_usecs;
    }

    while(1) {
	if( ! netjack_poll_deadline( driver->sockfd, driver->next_deadline ) )
	    break;

	packet_cache_drain_socket( global_packcache, driver->sockfd );

	if( packet_cache_get_next_available_framecnt( global_packcache, driver->expected_framecnt, &next_frame_avail) ) {
	    if( next_frame_avail == driver->expected_framecnt ) {
		we_have_the_expected_frame = 1;
		break;
	    }
	    //printf( "next frame = %d  (exp: %d) \n", next_frame_avail, driver->expected_framecnt );
	}
    }

    driver->running_free = 0;

    if( we_have_the_expected_frame ) {
	packet_cache_retreive_packet( global_packcache, driver->expected_framecnt, (char *) driver->rx_buf, driver->rx_bufsize );
	driver->packet_data_valid = 1;
	//printf( "ok... %d\n", driver->expected_framecnt );
    } else {
	// bah... the packet is not there.
	// either 
	// - it got lost.
	// - its late
	// - sync source is not sending anymore.
	
	// lets check if we have the next packets, we will just run a cycle without data.
	// in that case.
	
	if( packet_cache_get_next_available_framecnt( global_packcache, driver->expected_framecnt, &next_frame_avail) ) 
	{
	    jack_nframes_t offset = next_frame_avail - driver->expected_framecnt;

	    if( offset < driver->resync_threshold ) {
		// ok. dont do nothing. we will run without data. 
		// this seems to be one or 2 lost packets.
		driver->packet_data_valid = 0;
		//printf( "lost packet... %d\n", driver->expected_framecnt );
		
	    } else {
		// the diff is too high. but we have a packet.
		// lets resync.
		driver->expected_framecnt = next_frame_avail;
		packet_cache_retreive_packet( global_packcache, driver->expected_framecnt, (char *) driver->rx_buf, driver->rx_bufsize );
		driver->next_deadline_valid = 0;
		driver->packet_data_valid = 1;
		//printf( "resync... expected: %d, offset=%d\n", driver->expected_framecnt, offset );
	    }
	    
	} else {
	    // no packets in buffer.
	    driver->packet_data_valid = 0;

	    if( driver->num_lost_packets < 3 ) {
		// increase deadline.
		driver->next_deadline += driver->period_usecs/4;
		// might be lost packets.
		// continue
	    } else if( (driver->num_lost_packets <= 10) ) { 
		// lets try adjusting the deadline, for some packets, we might have just ran 2 fast.
	    } else {
		// give up. lets run freely.
		driver->running_free = 1;
		
		// But now we can check for any new frame available.
		if( packet_cache_get_next_available_framecnt( global_packcache, 0, &next_frame_avail) ) {
		    driver->expected_framecnt = next_frame_avail;
		    packet_cache_retreive_packet( global_packcache, driver->expected_framecnt, (char *) driver->rx_buf, driver->rx_bufsize );
		    driver->next_deadline_valid = 0;
		    driver->packet_data_valid = 1;
		    driver->running_free = 1;
		    //printf( "resync after freerun... %d\n", driver->expected_framecnt );
		}
	    }

	    //printf( "free... %d\n", driver->expected_framecnt );
	}
    }

    if( !driver->packet_data_valid )
	driver->num_lost_packets += 1;
    else {
	driver->num_lost_packets = 0;
	packet_header_ntoh (pkthdr);
    }

    framecnt = driver->expected_framecnt;
    driver->expected_framecnt += 1;

    
    driver->last_wait_ust = jack_get_microseconds ();
    driver->engine->transport_cycle_start (driver->engine, driver->last_wait_ust);

    /* this driver doesn't work so well if we report a delay */
    /* XXX: this might not be the case anymore */
    *delayed_usecs = 0;		/* lie about it */
    *status = 0;
    return driver->period_size;
}

static inline int
net_driver_run_cycle (net_driver_t *driver)
{
    jack_engine_t *engine = driver->engine;
    int wait_status = -1;
    float delayed_usecs;

    jack_nframes_t nframes = net_driver_wait (driver, -1, &wait_status,
                             &delayed_usecs);

    // XXX: xrun code removed.
    //      especially with celt there are no real xruns anymore.
    //      things are different on the net.

    if (wait_status == 0)
        return engine->run_cycle (engine, nframes, delayed_usecs);

    if (wait_status < 0)
        return -1;
    else
        return 0;
}

static int
net_driver_null_cycle (net_driver_t* driver, jack_nframes_t nframes)
{
    // TODO: talk to paul about this.
    //       do i wait here ?
    //       just sending out a packet marked with junk ?

    //int rx_size = get_sample_size(driver->bitdepth) * driver->capture_channels * driver->net_period_down + sizeof(jacknet_packet_header);
    int tx_size = get_sample_size(driver->bitdepth) * driver->playback_channels * driver->net_period_up + sizeof(jacknet_packet_header);
    unsigned int *packet_buf, *packet_bufX;

    packet_buf = alloca( tx_size);
    jacknet_packet_header *tx_pkthdr = (jacknet_packet_header *)packet_buf;
    jacknet_packet_header *rx_pkthdr = (jacknet_packet_header *)driver->rx_buf;

    //framecnt = rx_pkthdr->framecnt;

    driver->reply_port = rx_pkthdr->reply_port;

    // offset packet_bufX by the packetheader.
    packet_bufX = packet_buf + sizeof(jacknet_packet_header) / sizeof(jack_default_audio_sample_t);

    tx_pkthdr->sync_state = (driver->engine->control->sync_remain <= 1);

    tx_pkthdr->framecnt = framecnt;

    // memset 0 the payload.
    int payload_size = get_sample_size(driver->bitdepth) * driver->playback_channels * driver->net_period_up;
    memset(packet_bufX, 0, payload_size);

    packet_header_hton(tx_pkthdr);
    if (driver->srcaddress_valid)
        if (driver->reply_port)
            driver->syncsource_address.sin_port = htons(driver->reply_port);

    netjack_sendto(driver->outsockfd, (char *)packet_buf, tx_size,
                    0, (struct sockaddr*)&driver->syncsource_address, sizeof(struct sockaddr_in), driver->mtu);

    return 0;
}

static int
net_driver_bufsize (net_driver_t* driver, jack_nframes_t nframes)
{
    if (nframes != driver->period_size)
        return EINVAL;

    return 0;
}

static int
net_driver_read (net_driver_t* driver, jack_nframes_t nframes)
{
    jack_position_t local_trans_pos;
    jack_transport_state_t local_trans_state;

    unsigned int *packet_buf, *packet_bufX;

    if( ! driver->packet_data_valid ) {
	render_payload_to_jack_ports (driver->bitdepth, NULL, driver->net_period_down, driver->capture_ports, driver->capture_srcs, nframes);
	return 0;
    }
    packet_buf = driver->rx_buf;

    jacknet_packet_header *pkthdr = (jacknet_packet_header *)packet_buf;

    packet_bufX = packet_buf + sizeof(jacknet_packet_header) / sizeof(jack_default_audio_sample_t);

    //packet_header_ntoh(pkthdr);
    // fill framecnt from pkthdr.

    //if (pkthdr->framecnt != framecnt + 1)
    //    jack_info("bogus framecount %d", pkthdr->framecnt);

    framecnt = pkthdr->framecnt;
    driver->reply_port = pkthdr->reply_port;
    driver->latency = pkthdr->latency;
    driver->resync_threshold = pkthdr->latency-1;

    // check whether, we should handle the transport sync stuff, or leave trnasports untouched.
    if (driver->handle_transport_sync) {

        // read local transport info....
        local_trans_state = jack_transport_query(driver->client, &local_trans_pos);

        // Now check if we have to start or stop local transport to sync to remote...
        switch (pkthdr->transport_state) {
            case JackTransportStarting:
                // the master transport is starting... so we set our reply to the sync_callback;
                if (local_trans_state == JackTransportStopped) {
                    jack_transport_start(driver->client);
                    last_transport_state = JackTransportStopped;
                    sync_state = FALSE;
                    jack_info("locally stopped... starting...");
                }

                if (local_trans_pos.frame != (pkthdr->transport_frame + (pkthdr->latency) * nframes)) {
                    jack_transport_locate(driver->client, (pkthdr->transport_frame + (pkthdr->latency) * nframes));
                    last_transport_state = JackTransportRolling;
                    sync_state = FALSE;
                    jack_info("starting locate to %d", pkthdr->transport_frame + (pkthdr->latency)*nframes);
                }
                break;
            case JackTransportStopped:
                sync_state = TRUE;
                if (local_trans_pos.frame != (pkthdr->transport_frame)) {
                    jack_transport_locate(driver->client, (pkthdr->transport_frame));
                    jack_info("transport is stopped locate to %d", pkthdr->transport_frame);
                }
                if (local_trans_state != JackTransportStopped)
                    jack_transport_stop(driver->client);
                break;
            case JackTransportRolling:
                sync_state = TRUE;
//		    		if(local_trans_pos.frame != (pkthdr->transport_frame + (pkthdr->latency) * nframes)) {
//				    jack_transport_locate(driver->client, (pkthdr->transport_frame + (pkthdr->latency + 2) * nframes));
//				    jack_info("running locate to %d", pkthdr->transport_frame + (pkthdr->latency)*nframes);
//		    		}
                if (local_trans_state != JackTransportRolling)
                    jack_transport_start (driver->client);
                break;

            case JackTransportLooping:
                break;
        }
    }

    render_payload_to_jack_ports (driver->bitdepth, packet_bufX, driver->net_period_down, driver->capture_ports, driver->capture_srcs, nframes);

    return 0;
}

static int
net_driver_write (net_driver_t* driver, jack_nframes_t nframes)
{
    uint32_t *packet_buf, *packet_bufX;

    int packet_size = get_sample_size(driver->bitdepth) * driver->playback_channels * driver->net_period_up + sizeof(jacknet_packet_header);
    jacknet_packet_header *pkthdr; 

    packet_buf = alloca(packet_size);
    pkthdr = (jacknet_packet_header *)packet_buf;

    if( driver->running_free )
	return 0;

    // offset packet_bufX by the packetheader.
    packet_bufX = packet_buf + sizeof(jacknet_packet_header) / sizeof(jack_default_audio_sample_t);

    pkthdr->sync_state = (driver->engine->control->sync_remain <= 1);
    pkthdr->framecnt = framecnt;

    render_jack_ports_to_payload(driver->bitdepth, driver->playback_ports, driver->playback_srcs, nframes, packet_bufX, driver->net_period_up);

    packet_header_hton(pkthdr);
    if (driver->srcaddress_valid)
        if (driver->reply_port)
            driver->syncsource_address.sin_port = htons(driver->reply_port);

    netjack_sendto(driver->outsockfd, (char *)packet_buf, packet_size,
                    0, (struct sockaddr*)&driver->syncsource_address, sizeof(struct sockaddr_in), driver->mtu);

    return 0;
}


static int
net_driver_attach (net_driver_t *driver)
{
    //puts ("net_driver_attach");
    jack_port_t * port;
    char buf[32];
    unsigned int chn;
    int port_flags;

    driver->engine->set_buffer_size (driver->engine, driver->period_size);
    driver->engine->set_sample_rate (driver->engine, driver->sample_rate);

    if (driver->handle_transport_sync)
        jack_set_sync_callback(driver->client, (JackSyncCallback) net_driver_sync_cb, driver);

    port_flags = JackPortIsOutput | JackPortIsPhysical | JackPortIsTerminal;

    for (chn = 0; chn < driver->capture_channels_audio; chn++) {
        snprintf (buf, sizeof(buf) - 1, "capture_%u", chn + 1);

        port = jack_port_register (driver->client, buf,
                                   JACK_DEFAULT_AUDIO_TYPE,
                                   port_flags, 0);
        if (!port) {
            jack_error ("NET: cannot register port for %s", buf);
            break;
        }

        driver->capture_ports =
            jack_slist_append (driver->capture_ports, port);

	if( driver->bitdepth == 1000 ) {
#ifdef HAVE_CELT
	    // XXX: memory leak
	    CELTMode *celt_mode = celt_mode_create( driver->sample_rate, 1, driver->period_size, NULL );
	    driver->capture_srcs = jack_slist_append(driver->capture_srcs, celt_decoder_create( celt_mode ) );
#endif
	} else {
	    driver->capture_srcs = jack_slist_append(driver->capture_srcs, src_new(SRC_LINEAR, 1, NULL));
	}
    }
    for (chn = driver->capture_channels_audio; chn < driver->capture_channels; chn++) {
        snprintf (buf, sizeof(buf) - 1, "capture_%u", chn + 1);

        port = jack_port_register (driver->client, buf,
                                   JACK_DEFAULT_MIDI_TYPE,
                                   port_flags, 0);
        if (!port) {
            jack_error ("NET: cannot register port for %s", buf);
            break;
        }

        driver->capture_ports =
            jack_slist_append (driver->capture_ports, port);
        //driver->capture_srcs = jack_slist_append(driver->capture_srcs, src_new(SRC_LINEAR, 1, NULL));
    }

    port_flags = JackPortIsInput | JackPortIsPhysical | JackPortIsTerminal;

    for (chn = 0; chn < driver->playback_channels_audio; chn++) {
        snprintf (buf, sizeof(buf) - 1, "playback_%u", chn + 1);

        port = jack_port_register (driver->client, buf,
                                   JACK_DEFAULT_AUDIO_TYPE,
                                   port_flags, 0);

        if (!port) {
            jack_error ("NET: cannot register port for %s", buf);
            break;
        }

        driver->playback_ports =
            jack_slist_append (driver->playback_ports, port);
	if( driver->bitdepth == 1000 ) {
#ifdef HAVE_CELT
	    // XXX: memory leak
	    CELTMode *celt_mode = celt_mode_create( driver->sample_rate, 1, driver->period_size, NULL );
	    driver->playback_srcs = jack_slist_append(driver->playback_srcs, celt_encoder_create( celt_mode ) );
#endif
	} else {
	    driver->playback_srcs = jack_slist_append(driver->playback_srcs, src_new(SRC_LINEAR, 1, NULL));
	}
    }
    for (chn = driver->playback_channels_audio; chn < driver->playback_channels; chn++) {
        snprintf (buf, sizeof(buf) - 1, "playback_%u", chn + 1);

        port = jack_port_register (driver->client, buf,
                                   JACK_DEFAULT_MIDI_TYPE,
                                   port_flags, 0);

        if (!port) {
            jack_error ("NET: cannot register port for %s", buf);
            break;
        }

        driver->playback_ports =
            jack_slist_append (driver->playback_ports, port);
        //driver->playback_srcs = jack_slist_append(driver->playback_srcs, src_new(SRC_LINEAR, 1, NULL));
    }

    jack_activate (driver->client);
    return 0;
}

static int
net_driver_detach (net_driver_t *driver)
{
    JSList * node;

    if (driver->engine == 0)
        return 0;
//#if 0
    for (node = driver->capture_ports; node; node = jack_slist_next (node))
        jack_port_unregister (driver->client,
                              ((jack_port_t *) node->data));

    jack_slist_free (driver->capture_ports);
    driver->capture_ports = NULL;
//#endif

    for (node = driver->playback_ports; node; node = jack_slist_next (node))
        jack_port_unregister (driver->client,
                              ((jack_port_t *) node->data));

    jack_slist_free (driver->playback_ports);
    driver->playback_ports = NULL;

    return 0;
}

static void
net_driver_delete (net_driver_t *driver)
{
    jack_driver_nt_finish ((jack_driver_nt_t *) driver);
    free (driver);
}

static jack_driver_t *
net_driver_new (jack_client_t * client,
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
                unsigned int bitdepth)
{
    net_driver_t * driver;
    int first_pack_len;
    struct sockaddr_in address;

    jack_info ("creating net driver ... %s|%" PRIu32 "|%" PRIu32
            "|%u|%u|%u|transport_sync:%u", name, sample_rate, period_size, listen_port,
            capture_ports, playback_ports, transport_sync);

    driver = (net_driver_t *) calloc (1, sizeof (net_driver_t));

    jack_driver_nt_init ((jack_driver_nt_t *) driver);

    driver->write         = (JackDriverWriteFunction)      net_driver_write;
    driver->read          = (JackDriverReadFunction)       net_driver_read;
    driver->null_cycle    = (JackDriverNullCycleFunction)  net_driver_null_cycle;
    driver->nt_attach     = (JackDriverNTAttachFunction)   net_driver_attach;
    driver->nt_detach     = (JackDriverNTDetachFunction)   net_driver_detach;
    driver->nt_bufsize    = (JackDriverNTBufSizeFunction)  net_driver_bufsize;
    driver->nt_run_cycle  = (JackDriverNTRunCycleFunction) net_driver_run_cycle;

    // Fill in driver values.
    // might be subject to autoconfig...
    // so dont calculate anything with them...

    driver->sample_rate = sample_rate;
    driver->period_size = period_size;

    driver->listen_port   = listen_port;
    driver->last_wait_ust = 0;

    driver->capture_channels  = capture_ports + capture_ports_midi;
    driver->capture_channels_audio  = capture_ports;
    driver->capture_channels_midi   = capture_ports_midi;
    driver->capture_ports     = NULL;
    driver->playback_channels = playback_ports + playback_ports_midi;
    driver->playback_channels_audio = playback_ports;
    driver->playback_channels_midi = playback_ports_midi;
    driver->playback_ports    = NULL;

    driver->handle_transport_sync = transport_sync;
    driver->client = client;
    driver->engine = NULL;

    if ((bitdepth != 0) && (bitdepth != 8) && (bitdepth != 16) && (bitdepth != 1000))
    {
        jack_info ("Invalid bitdepth: %d (8, 16 or 0 for float) !!!", bitdepth);
        return NULL;
    }
    driver->bitdepth = bitdepth;


    if (resample_factor_up == 0)
        resample_factor_up = resample_factor;

    // Now open the socket, and wait for the first packet to arrive...
    driver->sockfd = socket (PF_INET, SOCK_DGRAM, 0);
    if (driver->sockfd == -1)
    {
        jack_info ("socket error");
        return NULL;
    }
    address.sin_family = AF_INET;
    address.sin_port = htons(driver->listen_port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind (driver->sockfd, (struct sockaddr *) &address, sizeof (address)) < 0)
    {
        jack_info("bind error");
        return NULL;
    }

    driver->outsockfd = socket (PF_INET, SOCK_DGRAM, 0);
    if (driver->sockfd == -1)
    {
        jack_info ("socket error");
        return NULL;
    }
    driver->srcaddress_valid = 0;

    jacknet_packet_header *first_packet = alloca (sizeof (jacknet_packet_header));
    socklen_t address_size = sizeof (struct sockaddr_in);

    jack_info ("Waiting for an incoming packet !!!");
    jack_info ("*** IMPORTANT *** Dont connect a client to jackd until the driver is attached to a clock source !!!");

    // XXX: netjack_poll polls forever.
    //      thats ok here.
    if (netjack_poll (driver->sockfd, 500))
        first_pack_len = recvfrom (driver->sockfd, first_packet, sizeof (jacknet_packet_header), 0, (struct sockaddr*) & driver->syncsource_address, &address_size);
    else
        first_pack_len = 0;

    driver->srcaddress_valid = 1;

    driver->mtu = 0;

    if (first_pack_len == sizeof (jacknet_packet_header))
    {
        packet_header_ntoh (first_packet);

        jack_info ("AutoConfig Override !!!");
        if (driver->sample_rate != first_packet->sample_rate)
        {
            jack_info ("AutoConfig Override: Master JACK sample rate = %d", first_packet->sample_rate);
            driver->sample_rate = first_packet->sample_rate;
        }

        if (driver->period_size != first_packet->period_size)
        {
            jack_info ("AutoConfig Override: Master JACK period size is %d", first_packet->period_size);
            driver->period_size = first_packet->period_size;
        }
        if (driver->capture_channels_audio != first_packet->capture_channels_audio)
        {
            jack_info ("AutoConfig Override: capture_channels_audio = %d", first_packet->capture_channels_audio);
            driver->capture_channels_audio = first_packet->capture_channels_audio;
        }
        if (driver->capture_channels_midi != first_packet->capture_channels_midi)
        {
            jack_info ("AutoConfig Override: capture_channels_midi = %d", first_packet->capture_channels_midi);
            driver->capture_channels_midi = first_packet->capture_channels_midi;
        }
        if (driver->playback_channels_audio != first_packet->playback_channels_audio)
        {
            jack_info ("AutoConfig Override: playback_channels_audio = %d", first_packet->playback_channels_audio);
            driver->playback_channels_audio = first_packet->playback_channels_audio;
        }
        if (driver->playback_channels_midi != first_packet->playback_channels_midi)
        {
            jack_info ("AutoConfig Override: playback_channels_midi = %d", first_packet->playback_channels_midi);
            driver->playback_channels_midi = first_packet->playback_channels_midi;
        }
        driver->capture_channels  = driver->capture_channels_audio + driver->capture_channels_midi;
        driver->playback_channels = driver->playback_channels_audio + driver->playback_channels_midi;

        driver->mtu = first_packet->mtu;
        jack_info ("MTU is set to %d bytes", first_packet->mtu);
        driver->latency = first_packet->latency;
    }

    // After possible Autoconfig: do all calculations...
    driver->period_usecs =
        (jack_time_t) floor ((((float) driver->period_size) / (float)driver->sample_rate)
                             * 1000000.0f);

    if( driver->bitdepth == 1000 ) {
	// celt mode. 
	// TODO: this is a hack. But i dont want to change the packet header.
	driver->net_period_down = resample_factor;
	driver->net_period_up = resample_factor_up;
    } else {
	driver->net_period_down = (float) driver->period_size / (float) resample_factor;
	driver->net_period_up = (float) driver->period_size / (float) resample_factor_up;
    }

    driver->rx_bufsize = sizeof (jacknet_packet_header) + driver->net_period_down * driver->capture_channels * get_sample_size (driver->bitdepth);
    driver->rx_buf = malloc (driver->rx_bufsize);
    driver->pkt_buf = malloc (driver->rx_bufsize);
    global_packcache = packet_cache_new (driver->latency + 5, driver->rx_bufsize, driver->mtu);

    driver->expected_framecnt_valid = 0;
    driver->num_lost_packets = 0;
    driver->next_deadline_valid = 0;

    driver->resync_threshold = driver->latency - 1;
    driver->running_free = 0;

    jack_info ("netjack: period   : up: %d / dn: %d", driver->net_period_up, driver->net_period_down);
    jack_info ("netjack: framerate: %d", driver->sample_rate);
    jack_info ("netjack: audio    : cap: %d / pbk: %d)", driver->capture_channels_audio, driver->playback_channels_audio);
    jack_info ("netjack: midi     : cap: %d / pbk: %d)", driver->capture_channels_midi, driver->playback_channels_midi);
    jack_info ("netjack: buffsize : rx: %d)", driver->rx_bufsize);
    return (jack_driver_t *) driver;
}

/* DRIVER "PLUGIN" INTERFACE */

jack_driver_desc_t *
driver_get_descriptor ()
{
    jack_driver_desc_t * desc;
    jack_driver_param_desc_t * params;
    unsigned int i;

    desc = calloc (1, sizeof (jack_driver_desc_t));
    strcpy (desc->name, "net");
    desc->nparams = 12;

    params = calloc (desc->nparams, sizeof (jack_driver_param_desc_t));

    i = 0;
    strcpy (params[i].name, "inchannels");
    params[i].character  = 'i';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 2U;
    strcpy (params[i].short_desc, "Number of capture channels (defaults to 2)");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "outchannels");
    params[i].character  = 'o';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 2U;
    strcpy (params[i].short_desc, "Number of playback channels (defaults to 2)");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "midi inchannels");
    params[i].character  = 'I';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 1U;
    strcpy (params[i].short_desc, "Number of midi capture channels (defaults to 1)");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "midi outchannels");
    params[i].character  = 'O';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 1U;
    strcpy (params[i].short_desc, "Number of midi playback channels (defaults to 1)");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "rate");
    params[i].character  = 'r';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 48000U;
    strcpy (params[i].short_desc, "Sample rate");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "period");
    params[i].character  = 'p';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 1024U;
    strcpy (params[i].short_desc, "Frames per period");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "listen-port");
    params[i].character  = 'l';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 3000U;
    strcpy (params[i].short_desc,
            "The socket port we are listening on for sync packets");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "factor");
    params[i].character  = 'f';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 1U;
    strcpy (params[i].short_desc,
            "Factor for sample rate reduction");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "upstream-factor");
    params[i].character  = 'u';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 0U;
    strcpy (params[i].short_desc,
            "Factor for sample rate reduction on the upstream");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "celt");
    params[i].character  = 'c';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 0U;
    strcpy (params[i].short_desc,
            "sets celt encoding and number of bytes per channel");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "bit-depth");
    params[i].character  = 'b';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 0U;
    strcpy (params[i].short_desc,
            "Sample bit-depth (0 for float, 8 for 8bit and 16 for 16bit)");
    strcpy (params[i].long_desc, params[i].short_desc);
    i++;

    strcpy (params[i].name, "transport-sync");
    params[i].character  = 't';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 1U;
    strcpy (params[i].short_desc,
            "Whether to slave the transport to the master transport");
    strcpy (params[i].long_desc, params[i].short_desc);

    desc->params = params;

    return desc;
}

const char driver_client_name[] = "net_pcm";

jack_driver_t *
driver_initialize (jack_client_t *client, const JSList * params)
{
    jack_nframes_t sample_rate = 48000;
    jack_nframes_t resample_factor = 1;
    jack_nframes_t period_size = 1024;
    unsigned int capture_ports = 2;
    unsigned int playback_ports = 2;
    unsigned int capture_ports_midi = 1;
    unsigned int playback_ports_midi = 1;
    unsigned int listen_port = 3000;
    unsigned int resample_factor_up = 0;
    unsigned int bitdepth = 0;
    unsigned int handle_transport_sync = 1;
    const JSList * node;
    const jack_driver_param_t * param;

    for (node = params; node; node = jack_slist_next (node)) {
        param = (const jack_driver_param_t *) node->data;

        switch (param->character) {

            case 'i':
                capture_ports = param->value.ui;
                break;

            case 'o':
                playback_ports = param->value.ui;
                break;

            case 'I':
                capture_ports_midi = param->value.ui;
                break;

            case 'O':
                playback_ports_midi = param->value.ui;
                break;

            case 'r':
                sample_rate = param->value.ui;
                break;

            case 'p':
                period_size = param->value.ui;
                break;

            case 'l':
                listen_port = param->value.ui;
                break;

            case 'f':
                resample_factor = param->value.ui;
                break;

            case 'u':
                resample_factor_up = param->value.ui;
                break;

            case 'b':
                bitdepth = param->value.ui;
                break;

	    case 'c':
#ifdef HAVE_CELT
		bitdepth = 1000;
		resample_factor = param->value.ui;
#else
		printf( "not built with celt support\n" );
		exit(10);
#endif
		break;

            case 't':
                handle_transport_sync = param->value.ui;
                break;
        }
    }

    return net_driver_new (client, "net_pcm", capture_ports, playback_ports,
                           capture_ports_midi, playback_ports_midi,
                           sample_rate, period_size,
                           listen_port, handle_transport_sync,
                           resample_factor, resample_factor_up, bitdepth);
}

void
driver_finish (jack_driver_t *driver)
{
    net_driver_delete ((net_driver_t *) driver);
}
