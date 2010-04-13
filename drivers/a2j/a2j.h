/* -*- Mode: C ; c-basic-offset: 2 -*- */
/*
 * ALSA SEQ < - > JACK MIDI bridge
 *
 * Copyright (c) 2006,2007 Dmitry S. Baikov <c0ff@konstruktiv.org>
 * Copyright (c) 2007,2008,2009 Nedko Arnaudov <nedko@arnaudov.name>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef STRUCTS_H__FD2CC895_411F_4ADE_9200_50FE395EDB72__INCLUDED
#define STRUCTS_H__FD2CC895_411F_4ADE_9200_50FE395EDB72__INCLUDED

#include <semaphore.h>
#include <jack/midiport.h>

#define JACK_INVALID_PORT NULL

#define MAX_PORTS  2048
#define MAX_EVENT_SIZE 1024

#define PORT_HASH_BITS 4
#define PORT_HASH_SIZE (1 << PORT_HASH_BITS)

typedef struct a2j_port * a2j_port_hash_t[PORT_HASH_SIZE];

struct a2j;

struct a2j_port
{
    struct a2j_port * next;       /* hash - jack */
    struct list_head siblings;    /* list - main loop */
    struct a2j * a2j_ptr;
    bool is_dead;
    char name[64];
    snd_seq_addr_t remote;
    jack_port_t * jack_port;
    
    jack_ringbuffer_t * inbound_events; // alsa_midi_event_t + data
    int64_t last_out_time;
    
    void * jack_buf;
};

struct a2j_stream
{
    snd_midi_event_t *codec;
    
    jack_ringbuffer_t *new_ports;
    
    a2j_port_hash_t port_hash;
    struct list_head list;
};

struct a2j
{
    jack_client_t * jack_client;
    
    snd_seq_t *seq;
    pthread_t alsa_io_thread;
    int client_id;
    int port_id;
    int queue;
    int input;
    int finishing;
    int ignore_hardware_ports;

    jack_ringbuffer_t* port_add; // snd_seq_addr_t
    jack_ringbuffer_t* port_del; // struct a2j_port*
    jack_ringbuffer_t* outbound_events; // struct a2j_delivery_event
    jack_nframes_t cycle_start;
    
    sem_t io_semaphore;

    struct a2j_stream stream;
};

#define NSEC_PER_SEC ((int64_t)1000*1000*1000)

struct a2j_alsa_midi_event
{
    int64_t time;
    int size;
};

#define MAX_JACKMIDI_EV_SIZE 16

struct a2j_delivery_event 
{
    struct list_head siblings;
    
    /* a jack MIDI event, plus the port its destined for: everything
       the ALSA output thread needs to deliver the event. time is
       part of the jack_event.
    */
    jack_midi_event_t jack_event;
    jack_nframes_t time; /* realtime, not offset time */
    struct a2j_port* port;
    char midistring[MAX_JACKMIDI_EV_SIZE];
};

void a2j_info (const char* fmt, ...);
void a2j_error (const char* fmt, ...);
void a2j_debug (const char* fmt, ...);
void a2j_warning (const char* fmt, ...);



#endif /* #ifndef STRUCTS_H__FD2CC895_411F_4ADE_9200_50FE395EDB72__INCLUDED */
