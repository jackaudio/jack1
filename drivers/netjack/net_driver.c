/* -*- mode: c; c-file-style: "linux"; -*- */
/*
NetJack Driver

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
#include <sys/poll.h>

#include <jack/types.h>
#include <jack/engine.h>
#include <sysdeps/time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <samplerate.h>

#include "net_driver.h"
#include "netjack_packet.h"

#undef DEBUG_WAKEUP

static int sync_state = TRUE;
static jack_transport_state_t last_transport_state;
static int framecnt; // this is set upon reading a packet.
// and will be written into the pkthdr of an outgoing packet.

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
net_driver_wait (net_driver_t *driver, int extra_fd, int *status,
                 float *delayed_usecs)
{
    // ok... we wait for a packet on the socket
    // TODO:
    // we should be able to run freely if the sync source is not transmitting
    // especially we should be able to compensate for packet loss somehow.
    // but lets try this out first.
    //
    // on packet loss we should either detect an xrun or just continue running when we
    // think, that the sync source is not running anymore.

#if 0
    fd_set read_fds;
    int res = 0;
    while (res == 0) {
        FD_ZERO(&read_fds);
        FD_SET(driver->sockfd, &read_fds);
        res = select(driver->sockfd + 1, &read_fds, NULL, NULL, NULL); // wait here until there is a packet to get
    }
#endif

    socklen_t address_size = sizeof(struct sockaddr_in);

    int bufsize =  get_sample_size(driver->bitdepth) * driver->capture_channels * driver->net_period_down + sizeof(jacknet_packet_header);

    jacknet_packet_header *pkthdr = (jacknet_packet_header *)driver->rx_buf;

rx_again:
    if (!driver->srcaddress_valid) {
        // XXX: Somthings really bad ;)
        puts("!driver->srcaddress_valid");
        return 0;
    }
    int len = netjack_recvfrom(driver->sockfd, (char *)driver->rx_buf, bufsize,
                                MSG_WAITALL, (struct sockaddr*) & driver->syncsource_address, &address_size, 1400);


    if (len != bufsize) {
        jack_info("wrong_packet_len: len=%d, expected=%d", len, bufsize);
        goto rx_again;
    }

    packet_header_ntoh(pkthdr);

    //driver->srcaddress_valid = 0;

    driver->last_wait_ust = jack_get_microseconds ();
    driver->engine->transport_cycle_start (driver->engine,
                                           driver->last_wait_ust);

    /* this driver doesn't work so well if we report a delay */
    *delayed_usecs = 0;		/* lie about it */
    *status = 0;
    return driver->period_size;
}

static inline int
net_driver_run_cycle (net_driver_t *driver)
{
    jack_engine_t *engine = driver->engine;
    int wait_status;
    float delayed_usecs;

    jack_nframes_t nframes = net_driver_wait (driver, -1, &wait_status,
                             &delayed_usecs);

    // currently there is no xrun detection.
    // so nframes will always be period_size.
    // XXX: i uncomment this code because the signature of delay()
    //      changed samewhere in the 0.99.x series. so this is the showstopper for 0.99.0

#if 0
    if (nframes == 0) {
        /* we detected an xrun and restarted: notify
        * clients about the delay. */
        engine->delay (engine, delayed_usecs);
        return 0;
    }
#endif

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
    //int rx_size = get_sample_size(driver->bitdepth) * driver->capture_channels * driver->net_period_down + sizeof(jacknet_packet_header);
    int tx_size = get_sample_size(driver->bitdepth) * driver->playback_channels * driver->net_period_up + sizeof(jacknet_packet_header);
    unsigned int *packet_buf, *packet_bufX;

    packet_buf = alloca( tx_size);
    jacknet_packet_header *tx_pkthdr = (jacknet_packet_header *)packet_buf;
    jacknet_packet_header *rx_pkthdr = (jacknet_packet_header *)driver->rx_buf;

    framecnt = rx_pkthdr->framecnt;

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
                    0, (struct sockaddr*)&driver->syncsource_address, sizeof(struct sockaddr_in), 1400);

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
    //jack_default_audio_sample_t* buf;
    //jack_port_t *port;
    jack_position_t local_trans_pos;
    jack_transport_state_t local_trans_state;

    //int bufsize =  get_sample_size(driver->bitdepth) * driver->capture_channels * driver->net_period_down + sizeof(jacknet_packet_header);
    unsigned int *packet_buf, *packet_bufX;

    packet_buf = driver->rx_buf;

    jacknet_packet_header *pkthdr = (jacknet_packet_header *)packet_buf;

    packet_bufX = packet_buf + sizeof(jacknet_packet_header) / sizeof(jack_default_audio_sample_t);

    //packet_header_ntoh(pkthdr);
    // fill framecnt from pkthdr.

    if (pkthdr->framecnt != framecnt + 1)
        jack_info("bogus framecount %d", pkthdr->framecnt);

    framecnt = pkthdr->framecnt;
    driver->reply_port = pkthdr->reply_port;

    // check whether, we should handle the transport sync stuff, or leave trnasports untouched.
    if (driver->handle_transport_sync) {

        // read local transport info....
        local_trans_state = jack_transport_query(driver->client, &local_trans_pos);

        // Now check if we have to start or stop local transport to sync to remote...
        switch (pkthdr->transport_state) {
            case JackTransportStarting:
                // the master transport is starting... so we set our reply to the sync_callback;
                if (local_trans_state == JackTransportStopped) {
                    ja(ck_transport_start(driver->client);
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
                    jack_transport_start(driver->client);
                break;

            case JackTransportLooping:
                break;
        }
    }


    render_payload_to_jack_ports(driver->bitdepth, packet_bufX, driver->net_period_down, driver->capture_ports, driver->capture_srcs, nframes);

    return 0;
}

static int
net_driver_write (net_driver_t* driver, jack_nframes_t nframes)
{
    uint32_t *packet_buf, *packet_bufX;

    int packet_size = get_sample_size(driver->bitdepth) * driver->playback_channels * driver->net_period_up + sizeof(jacknet_packet_header);

    packet_buf = alloca(packet_size);

    jacknet_packet_header *pkthdr = (jacknet_packet_header *)packet_buf;

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
                    0, (struct sockaddr*)&driver->syncsource_address, sizeof(struct sockaddr_in), 1400);

    return 0;
}


static int
net_driver_attach (net_driver_t *driver)
{
    puts ("net_driver_attach");
    jack_port_t * port;
    char buf[32];
    unsigned int chn;
    int port_flags;

    driver->engine->set_buffer_size (driver->engine, driver->period_size);
    driver->engine->set_sample_rate (driver->engine, driver->sample_rate);

    if (driver->handle_transport_sync)
        jack_set_sync_callback(driver->client, (JackSyncCallback) net_driver_sync_cb, driver);

    port_flags = JackPortIsOutput | JackPortIsPhysical | JackPortIsTerminal;

    for (chn = 0; chn < driver->capture_channels; chn++) {
        snprintf (buf, sizeof(buf) - 1, "capture_%u", chn + 1);

        port = jack_port_register (driver->client, buf,
                                   JACK_DEFAULT_AUDIO_TYPE,
                                   port_flags, 0);
        if (!port) {
            jack_error ("DUMMY: cannot register port for %s", buf);
            break;
        }

        driver->capture_ports =
            jack_slist_append (driver->capture_ports, port);
        driver->capture_srcs = jack_slist_append(driver->capture_srcs, src_new(SRC_LINEAR, 1, NULL));
    }

    port_flags = JackPortIsInput | JackPortIsPhysical | JackPortIsTerminal;

    for (chn = 0; chn < driver->playback_channels; chn++) {
        snprintf (buf, sizeof(buf) - 1, "playback_%u", chn + 1);

        port = jack_port_register (driver->client, buf,
                                   JACK_DEFAULT_AUDIO_TYPE,
                                   port_flags, 0);

        if (!port) {
            jack_error ("DUMMY: cannot register port for %s", buf);
            break;
        }

        driver->playback_ports =
            jack_slist_append (driver->playback_ports, port);
        driver->playback_srcs = jack_slist_append(driver->playback_srcs, src_new(SRC_LINEAR, 1, NULL));
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
                jack_nframes_t sample_rate,
                jack_nframes_t period_size,
                unsigned int listen_port,
                unsigned int transport_sync,
                unsigned int resample_factor,
                unsigned int resample_factor_up,
                unsigned int bitdepth)
{
    net_driver_t * driver;
    struct sockaddr_in address;

    jack_info("creating net driver ... %s|%" PRIu32 "|%" PRIu32
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

    driver->capture_channels  = capture_ports;
    driver->capture_ports     = NULL;
    driver->playback_channels = playback_ports;
    driver->playback_ports    = NULL;

    driver->handle_transport_sync = transport_sync;
    driver->client = client;
    driver->engine = NULL;

    if ((bitdepth != 0) && (bitdepth != 8) && (bitdepth != 16)) {
        jack_info("Invalid bitdepth: %d (8,16 or 0 for float) !!!", bitdepth);
        return NULL;
    }
    driver->bitdepth = bitdepth;


    if (resample_factor_up == 0)
        resample_factor_up = resample_factor;

    // Now open the socket, and wait for the first packet to arrive...


    driver->sockfd = socket(PF_INET, SOCK_DGRAM, 0);
    if (driver->sockfd == -1) {
        jack_info("socket error");
        return NULL;
    }
    address.sin_family = AF_INET;
    address.sin_port = htons(driver->listen_port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(driver->sockfd, (struct sockaddr *) &address, sizeof(address)) < 0) {
        jack_info("bind error");
        return NULL;
    }

    driver->outsockfd = socket(PF_INET, SOCK_DGRAM, 0);
    if (driver->sockfd == -1) {
        jack_info("socket error");
        return NULL;
    }
    driver->srcaddress_valid = 0;

    jacknet_packet_header *first_packet = alloca(sizeof(jacknet_packet_header));
    socklen_t address_size = sizeof(struct sockaddr_in);

    jack_info("Waiting for an incoming packet !!!");
    jack_info("*** IMPORTANT *** Dont connect a client to jackd until the driver is attached to a clock source !!!");

    int first_pack_len = recvfrom(driver->sockfd, first_packet, sizeof(jacknet_packet_header), 0, (struct sockaddr*) & driver->syncsource_address, &address_size);
    driver->srcaddress_valid = 1;

    jack_info("first_pack_len=%d", first_pack_len);
    // A packet is here.... If it wasnt the old trigger packet containing only 'x' evaluate the autoconf data...

    driver->mtu = 0;

    if (first_pack_len == sizeof(jacknet_packet_header)) {
        packet_header_ntoh(first_packet);

        jack_info("AutoConfig Override !!!");
        if (driver->sample_rate != first_packet->sample_rate) {
            jack_info("AutoConfig Override: sample_rate = %d", first_packet->sample_rate);
            driver->sample_rate = first_packet->sample_rate;
        }

        if (driver->period_size != first_packet->period_size) {
            jack_info("AutoConfig Override: period_size = %d", first_packet->period_size);
            driver->period_size = first_packet->period_size;
        }

        driver->mtu = first_packet->mtu;
        driver->latency = first_packet->latency;
    }

    // After possible Autoconfig: do all calculations...

    driver->period_usecs =
        (jack_time_t) floor ((((float) driver->period_size) / driver->sample_rate)
                             * 1000000.0f);

    driver->net_period_down = (float) driver->period_size / (float) resample_factor;
    driver->net_period_up = (float) driver->period_size / (float) resample_factor_up;

    driver->rx_buf = malloc(sizeof(jacknet_packet_header) + driver->net_period_down * driver->capture_channels * get_sample_size(driver->bitdepth));

    // XXX: dont need it when packet size < mtu
    driver->pkt_buf = malloc(sizeof(jacknet_packet_header) + driver->net_period_down * driver->capture_channels * get_sample_size(driver->bitdepth));

    int rx_bufsize = sizeof(jacknet_packet_header) + driver->net_period_down * driver->capture_channels * get_sample_size(driver->bitdepth);
    global_packcache = packet_cache_new(driver->latency + 5, rx_bufsize, 1400);

    jack_info("net_period: (up:%d/dn:%d)", driver->net_period_up, driver->net_period_down);
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
    desc->nparams = 9;

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

            case 't':
                handle_transport_sync = param->value.ui;
                break;
        }
    }

    return net_driver_new (client, "net_pcm", capture_ports,
                           playback_ports, sample_rate, period_size,
                           listen_port, handle_transport_sync,
                           resample_factor, resample_factor_up, bitdepth);
}

void
driver_finish (jack_driver_t *driver)
{
    net_driver_delete ((net_driver_t *) driver);
}
