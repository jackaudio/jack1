
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


#include <math.h>
#include <stdio.h>
#include <memory.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>

#include <jack/types.h>
#include <jack/engine.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <samplerate.h>

#include "net_driver.h"
#include "netjack_packet.h"

int fraggo = 0;

void
packet_header_hton(jacknet_packet_header *pkthdr)
{
    pkthdr->channels = htonl(pkthdr->channels);
    pkthdr->period_size = htonl(pkthdr->period_size);
    pkthdr->sample_rate = htonl(pkthdr->sample_rate);
    pkthdr->sync_state = htonl(pkthdr->sync_state);
    pkthdr->transport_frame = htonl(pkthdr->transport_frame);
    pkthdr->transport_state = htonl(pkthdr->transport_state);
    pkthdr->framecnt = htonl(pkthdr->framecnt);
    pkthdr->latency = htonl(pkthdr->latency);
    pkthdr->reply_port = htonl(pkthdr->reply_port);
    pkthdr->mtu = htonl(pkthdr->mtu);
    pkthdr->fragment_nr = htonl(pkthdr->fragment_nr);
}

void
packet_header_ntoh(jacknet_packet_header *pkthdr)
{
    pkthdr->channels = ntohl(pkthdr->channels);
    pkthdr->period_size = ntohl(pkthdr->period_size);
    pkthdr->sample_rate = ntohl(pkthdr->sample_rate);
    pkthdr->sync_state = ntohl(pkthdr->sync_state);
    pkthdr->transport_frame = ntohl(pkthdr->transport_frame);
    pkthdr->transport_state = ntohl(pkthdr->transport_state);
    pkthdr->framecnt = ntohl(pkthdr->framecnt);
    pkthdr->latency = ntohl(pkthdr->latency);
    pkthdr->reply_port = ntohl(pkthdr->reply_port);
    pkthdr->mtu = ntohl(pkthdr->mtu);
    pkthdr->fragment_nr = ntohl(pkthdr->fragment_nr);
}

int get_sample_size(int bitdepth)
{
    if (bitdepth == 8)
        return sizeof(int8_t);
    if (bitdepth == 16)
        return sizeof(int16_t);

    return sizeof(int32_t);
}


// fragmented packet IO

int netjack_recvfrom(int sockfd, char *packet_buf, int pkt_size, int flags, struct sockaddr *addr, socklen_t *addr_size, int mtu)
{
    char *rx_packet;
    char *dataX;

    int fragment_payload_size;

    // Copy the packet header to the tx pack first.
    //memcpy(tx_packet, packet_buf, sizeof(jacknet_packet_header));

    jacknet_packet_header *pkthdr;

    // Now loop and send all
    char *packet_bufX;

    // wait for fragment_nr == 0
    int rcv_len;

    rx_packet = alloca(mtu);
    dataX = rx_packet + sizeof(jacknet_packet_header);

    fragment_payload_size = mtu - sizeof(jacknet_packet_header);

    // Copy the packet header to the tx pack first.
    //memcpy(tx_packet, packet_buf, sizeof(jacknet_packet_header));

    pkthdr = (jacknet_packet_header *)rx_packet;

    // Now loop and send all
    packet_bufX = packet_buf + sizeof(jacknet_packet_header);


    if (pkt_size <= mtu)
    {
        return recvfrom(sockfd, packet_buf, pkt_size, flags, addr, addr_size);
    } else
    {
rx_again:
        rcv_len = recvfrom(sockfd, rx_packet, mtu, 0, addr, addr_size);
        if (rcv_len < 0)
            return rcv_len;

        if (rcv_len >= sizeof(jacknet_packet_header)) {
            //printf("got fragmentooooo_nr = %d recv_len = %d\n",  ntohl(pkthdr->fragment_nr), rcv_len);
            if ((ntohl(pkthdr->fragment_nr)) != 0)
                goto rx_again;
        } else {
            goto rx_again;
        }
        //goto rx_again;

        //printf("ok... lets go...\n");
        // ok... we have read a fragement 0;
        // copy the packet header...
        memcpy(packet_buf, rx_packet, sizeof(jacknet_packet_header));

        int fragment_count = 0;

        while (packet_bufX <= (packet_buf + pkt_size - fragment_payload_size)) {

            //printf("enter loop: fragment_count = %d, pkthdr->fragment_nr = %d\n", fragment_count, pkthdr->fragment_nr);
            // check fragment number.
            if ((ntohl(pkthdr->fragment_nr)) != fragment_count) {
                printf("got unexpected fragment %d (expected %d)\n", ntohl(pkthdr->fragment_nr), fragment_count);
                return sizeof(jacknet_packet_header) + (fragment_count) * fragment_payload_size;
            } else
                //printf("expected fragment %d\n", fragment_count);

                // copy the payload into the packet buffer...
                memcpy(packet_bufX, dataX, fragment_payload_size);

            rcv_len = recvfrom(sockfd, rx_packet, mtu, 0, addr, addr_size);
            //printf("got fragmen_nr = %d rcv_len = %d\n",  ntohl(pkthdr->fragment_nr), rcv_len);
            //printf("got fragmen_nr = %d\n",  ntohl(pkthdr->fragment_nr));
            if (rcv_len < 0)
                return -1;

            packet_bufX += fragment_payload_size;
            fragment_count++;
        }

        //printf("at the end rcv_len = %d\n ", rcv_len);
        int last_payload_size = packet_bufX - packet_buf - pkt_size;
        memcpy(packet_bufX, dataX, rcv_len - sizeof(jacknet_packet_header));
    }
    return pkt_size;
}

int netjack_recv(int sockfd, char *packet_buf, int pkt_size, int flags, int mtu)
{
    if (pkt_size <= mtu) {
        return recv(sockfd, packet_buf, pkt_size, flags);
    } else {
        char *rx_packet = alloca(mtu);
        char *dataX = rx_packet + sizeof(jacknet_packet_header);

        int fragment_payload_size = mtu - sizeof(jacknet_packet_header);
        jacknet_packet_header *pkthdr = (jacknet_packet_header *)rx_packet;

        // Now loop and send all
        char *packet_bufX = packet_buf + sizeof(jacknet_packet_header);

        // wait for fragment_nr == 0
        int rcv_len;
rx_again:
        rcv_len = recv(sockfd, rx_packet, mtu, flags);
        if (rcv_len < 0)
            return rcv_len;

        if (rcv_len >= sizeof(jacknet_packet_header)) {
            //printf("got fragmentooo_nr = %d\n",  ntohl(pkthdr->fragment_nr));
            if (ntohl(pkthdr->fragment_nr) != 0)
                goto rx_again;
        } else {
            goto rx_again;
        }

        //printf("ok we got a fragment 0\n");
        // ok... we have read a fragement 0;
        // copy the packet header...
        memcpy(packet_buf, rx_packet, sizeof(jacknet_packet_header));

        int fragment_count = 0;

        while (packet_bufX <= (packet_buf + pkt_size - fragment_payload_size)) {

            // check fragment number.
            if (ntohl(pkthdr->fragment_nr) != fragment_count) {
                printf("got unexpected fragment %d (expected %d)\n", ntohl(pkthdr->fragment_nr), fragment_count);
                return sizeof(jacknet_packet_header) + (fragment_count - 1) * fragment_payload_size;
            }

            // copy the payload into the packet buffer...
            memcpy(packet_bufX, dataX, fragment_payload_size);

            rcv_len = recv(sockfd, rx_packet, mtu, flags);
            //printf("got fragmen_nr = %d rcv_len = %d\n",  ntohl(pkthdr->fragment_nr), rcv_len);
            if (rcv_len < 0)
                return -1;

            packet_bufX += fragment_payload_size;
            fragment_count++;
        }

        //int last_payload_size = packet_bufX - packet_buf - pkt_size;
        //memcpy(packet_bufX, dataX, rcv_len - sizeof(jacknet_packet_header));
    }
    return pkt_size;
}

void netjack_sendto(int sockfd, char *packet_buf, int pkt_size, int flags, struct sockaddr *addr, int addr_size, int mtu)
{
    int frag_cnt = 0;
    char *tx_packet, *dataX;
    jacknet_packet_header *pkthdr;

    tx_packet = alloca(mtu + 10);
    dataX = tx_packet + sizeof(jacknet_packet_header);
    pkthdr = (jacknet_packet_header *)tx_packet;

    int fragment_payload_size = mtu - sizeof(jacknet_packet_header);

    if (pkt_size <= mtu) {
        sendto(sockfd, packet_buf, pkt_size, flags, addr, addr_size);
    } else {

        // Copy the packet header to the tx pack first.
        memcpy(tx_packet, packet_buf, sizeof(jacknet_packet_header));

        // Now loop and send all
        char *packet_bufX = packet_buf + sizeof(jacknet_packet_header);


        while (packet_bufX < (packet_buf + pkt_size - fragment_payload_size)) {
            pkthdr->fragment_nr = htonl(frag_cnt++);
            memcpy(dataX, packet_bufX, fragment_payload_size);

            int err = sendto(sockfd, tx_packet, mtu, flags, addr, addr_size);

            packet_bufX += fragment_payload_size;
        }

        int last_payload_size = packet_buf + pkt_size - packet_bufX;
        memcpy(dataX, packet_bufX, last_payload_size);
        pkthdr->fragment_nr = htonl(frag_cnt);
        //printf("last fragment_count = %d, payload_size = %d\n", fragment_count, last_payload_size);

        // sendto(last_pack_size);
        sendto(sockfd, tx_packet, last_payload_size + sizeof(jacknet_packet_header), flags, addr, addr_size);
    }
}

// render functions for float
void render_payload_to_jack_ports_float( void *packet_payload, jack_nframes_t net_period_down, JSList *capture_ports, JSList *capture_srcs, jack_nframes_t nframes)
{
    channel_t chn = 0;
    JSList *node = capture_ports;
    JSList *src_node = capture_srcs;

    uint32_t *packet_bufX = (uint32_t *)packet_payload;

    while (node != NULL) {
        int i;
        int_float_t val;
        SRC_DATA src;

        jack_port_t *port = (jack_port_t *) node->data;
        jack_default_audio_sample_t* buf = jack_port_get_buffer (port, nframes);

        if (net_period_down != nframes) {
            SRC_STATE *src_state = src_node->data;
            for (i = 0; i < net_period_down; i++) {
                packet_bufX[i] = ntohl(packet_bufX[i]);
            }

            src.data_in = (float *)packet_bufX;
            src.input_frames = net_period_down;

            src.data_out = buf;
            src.output_frames = nframes;

            src.src_ratio = (float) nframes / (float) net_period_down;
            src.end_of_input = 0;

            src_set_ratio(src_state, src.src_ratio);
            src_process(src_state, &src);
            src_node = jack_slist_next (src_node);
        } else {
            for (i = 0; i < net_period_down; i++) {
                val.i = packet_bufX[i];
                val.i = ntohl(val.i);
                buf[i] = val.f;
            }
        }

        packet_bufX = (packet_bufX + net_period_down);
        node = jack_slist_next (node);
        chn++;
    }
}

void render_jack_ports_to_payload_float (JSList *playback_ports, JSList *playback_srcs, jack_nframes_t nframes, void *packet_payload, jack_nframes_t net_period_up)
{
    channel_t chn = 0;
    JSList *node = playback_ports;
    JSList *src_node = playback_srcs;

    uint32_t *packet_bufX = (uint32_t *)packet_payload;

    while (node != NULL) {
        SRC_DATA src;
        int i;
        int_float_t val;
        jack_port_t *port = (jack_port_t *) node->data;
        jack_default_audio_sample_t* buf = jack_port_get_buffer (port, nframes);

        if (net_period_up != nframes) {
            SRC_STATE *src_state = src_node->data;
            src.data_in = buf;
            src.input_frames = nframes;

            src.data_out = (float *) packet_bufX;
            src.output_frames = net_period_up;

            src.src_ratio = (float) net_period_up / (float) nframes;
            src.end_of_input = 0;

            src_set_ratio(src_state, src.src_ratio);
            src_process(src_state, &src);

            for (i = 0; i < net_period_up; i++) {
                packet_bufX[i] = htonl(packet_bufX[i]);
            }
            src_node = jack_slist_next (src_node);
        } else {
            for (i = 0; i < net_period_up; i++) {
                val.f = buf[i];
                val.i = htonl(val.i);
                packet_bufX[i] = val.i;
            }
        }

        packet_bufX = (packet_bufX + net_period_up);
        node = jack_slist_next (node);
        chn++;
    }
}

// render functions for 16bit
void render_payload_to_jack_ports_16bit( void *packet_payload, jack_nframes_t net_period_down, JSList *capture_ports, JSList *capture_srcs, jack_nframes_t nframes)
{
    channel_t chn = 0;
    JSList *node = capture_ports;
    JSList *src_node = capture_srcs;

    uint16_t *packet_bufX = (uint16_t *)packet_payload;

    while (node != NULL) {
        int i;
        //uint32_t val;
        SRC_DATA src;

        jack_port_t *port = (jack_port_t *) node->data;
        jack_default_audio_sample_t* buf = jack_port_get_buffer (port, nframes);

        float *floatbuf = alloca(sizeof(float) * net_period_down);

        if (net_period_down != nframes) {
            SRC_STATE *src_state = src_node->data;
            for (i = 0; i < net_period_down; i++) {
                floatbuf[i] = ((float) ntohs(packet_bufX[i])) / 32767.0 - 1.0;
            }

            src.data_in = floatbuf;
            src.input_frames = net_period_down;

            src.data_out = buf;
            src.output_frames = nframes;

            src.src_ratio = (float) nframes / (float) net_period_down;
            src.end_of_input = 0;

            src_set_ratio(src_state, src.src_ratio);
            src_process(src_state, &src);
            src_node = jack_slist_next (src_node);
        } else {
            for (i = 0; i < net_period_down; i++) {
                buf[i] = ((float) ntohs(packet_bufX[i])) / 32768.0 - 1.0;
            }
        }

        packet_bufX = (packet_bufX + net_period_down);
        node = jack_slist_next (node);
        chn++;
    }
}

void render_jack_ports_to_payload_16bit (JSList *playback_ports, JSList *playback_srcs, jack_nframes_t nframes, void *packet_payload, jack_nframes_t net_period_up)
{
    channel_t chn = 0;
    JSList *node = playback_ports;
    JSList *src_node = playback_srcs;

    uint16_t *packet_bufX = (uint16_t *)packet_payload;

    while (node != NULL) {
        SRC_DATA src;
        int i;
        jack_port_t *port = (jack_port_t *) node->data;
        jack_default_audio_sample_t* buf = jack_port_get_buffer (port, nframes);

        if (net_period_up != nframes) {
            SRC_STATE *src_state = src_node->data;

            float *floatbuf = alloca(sizeof(float) * net_period_up);

            src.data_in = buf;
            src.input_frames = nframes;

            src.data_out = floatbuf;
            src.output_frames = net_period_up;

            src.src_ratio = (float) net_period_up / (float) nframes;
            src.end_of_input = 0;

            src_set_ratio(src_state, src.src_ratio);
            src_process(src_state, &src);

            for (i = 0; i < net_period_up; i++) {
                packet_bufX[i] = htons(((uint16_t)((floatbuf[i] + 1.0) * 32767.0)));
            }
            src_node = jack_slist_next (src_node);
        } else {
            for (i = 0; i < net_period_up; i++) {
                packet_bufX[i] = htons(((uint16_t)((buf[i] + 1.0) * 32767.0)));
            }
        }

        packet_bufX = (packet_bufX + net_period_up);
        node = jack_slist_next (node);
        chn++;
    }
}

// render functions for 8bit

void render_payload_to_jack_ports_8bit(void *packet_payload, jack_nframes_t net_period_down, JSList *capture_ports, JSList *capture_srcs, jack_nframes_t nframes)
{
    channel_t chn = 0;
    JSList *node = capture_ports;
    JSList *src_node = capture_srcs;

    int8_t *packet_bufX = (int8_t *)packet_payload;

    while (node != NULL) {
        int i;
        //uint32_t val;
        SRC_DATA src;

        jack_port_t *port = (jack_port_t *) node->data;
        jack_default_audio_sample_t* buf = jack_port_get_buffer (port, nframes);

        float *floatbuf = alloca(sizeof(float) * net_period_down);

        if (net_period_down != nframes) {
            SRC_STATE *src_state = src_node->data;
            for (i = 0; i < net_period_down; i++) {
                floatbuf[i] = ((float) packet_bufX[i]) / 127.0;
            }

            src.data_in = floatbuf;
            src.input_frames = net_period_down;

            src.data_out = buf;
            src.output_frames = nframes;

            src.src_ratio = (float) nframes / (float) net_period_down;
            src.end_of_input = 0;

            src_set_ratio(src_state, src.src_ratio);
            src_process(src_state, &src);
            src_node = jack_slist_next (src_node);
        } else {
            for (i = 0; i < net_period_down; i++) {
                buf[i] = ((float) packet_bufX[i]) / 127.0;
            }
        }

        packet_bufX = (packet_bufX + net_period_down);
        node = jack_slist_next (node);
        chn++;
    }
}

void render_jack_ports_to_payload_8bit(JSList *playback_ports, JSList *playback_srcs, jack_nframes_t nframes, void *packet_payload, jack_nframes_t net_period_up)
{
    channel_t chn = 0;
    JSList *node = playback_ports;
    JSList *src_node = playback_srcs;

    int8_t *packet_bufX = (int8_t *)packet_payload;

    while (node != NULL) {
        SRC_DATA src;
        int i;
        jack_port_t *port = (jack_port_t *) node->data;
        jack_default_audio_sample_t* buf = jack_port_get_buffer (port, nframes);

        if (net_period_up != nframes) {
            SRC_STATE *src_state = src_node->data;

            float *floatbuf = alloca(sizeof(float) * net_period_up);

            src.data_in = buf;
            src.input_frames = nframes;

            src.data_out = floatbuf;
            src.output_frames = net_period_up;

            src.src_ratio = (float) net_period_up / (float) nframes;
            src.end_of_input = 0;

            src_set_ratio(src_state, src.src_ratio);
            src_process(src_state, &src);

            for (i = 0; i < net_period_up; i++) {
                packet_bufX[i] = floatbuf[i] * 127.0;
            }
            src_node = jack_slist_next (src_node);
        } else {
            for (i = 0; i < net_period_up; i++) {
                packet_bufX[i] = buf[i] * 127.0;
            }
        }

        packet_bufX = (packet_bufX + net_period_up);
        node = jack_slist_next (node);
        chn++;
    }
}

// wrapper functions with bitdepth argument...
void render_payload_to_jack_ports(int bitdepth, void *packet_payload, jack_nframes_t net_period_down, JSList *capture_ports, JSList *capture_srcs, jack_nframes_t nframes)
{
    if (bitdepth == 8)
        render_payload_to_jack_ports_8bit(packet_payload, net_period_down, capture_ports, capture_srcs, nframes);
    else if (bitdepth == 16)
        render_payload_to_jack_ports_16bit(packet_payload, net_period_down, capture_ports, capture_srcs, nframes);
    else
        render_payload_to_jack_ports_float(packet_payload, net_period_down, capture_ports, capture_srcs, nframes);
}

void render_jack_ports_to_payload(int bitdepth, JSList *playback_ports, JSList *playback_srcs, jack_nframes_t nframes, void *packet_payload, jack_nframes_t net_period_up)
{
    if (bitdepth == 8)
        render_jack_ports_to_payload_8bit(playback_ports, playback_srcs, nframes, packet_payload, net_period_up);
    else if (bitdepth == 16)
        render_jack_ports_to_payload_16bit(playback_ports, playback_srcs, nframes, packet_payload, net_period_up);
    else
        render_jack_ports_to_payload_float(playback_ports, playback_srcs, nframes, packet_payload, net_period_up);
}
