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

#ifndef __jack_alsa_midi_h__
#define __jack_alsa_midi_h__

#include <stdbool.h>
#include <semaphore.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>

#include "driver.h"
#include "list.h"

#define JACK_INVALID_PORT NULL

#define MAX_PORTS  2048
#define MAX_EVENT_SIZE 1024

#define PORT_HASH_BITS 4
#define PORT_HASH_SIZE (1 << PORT_HASH_BITS)

/* Beside enum use, these are indeces for (struct a2j).stream array */
#define A2J_PORT_CAPTURE   0 // ALSA playback port -> JACK capture port
#define A2J_PORT_PLAYBACK  1 // JACK playback port -> ALSA capture port

typedef struct a2j_port * a2j_port_hash_t[PORT_HASH_SIZE];

struct alsa_midi_driver;

struct a2j_port
{
    struct a2j_port * next;       /* hash - jack */
    struct list_head siblings;    /* list - main loop */
    struct alsa_midi_driver * driver_ptr;
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

typedef struct alsa_midi_driver
{
    JACK_DRIVER_DECL;

    jack_client_t * jack_client;
    
    snd_seq_t *seq;
    pthread_t alsa_input_thread;
    pthread_t alsa_output_thread;
    int client_id;
    int port_id;
    int queue;
    bool freewheeling;
    bool running;
    bool finishing;

    jack_ringbuffer_t* port_add; // snd_seq_addr_t
    jack_ringbuffer_t* port_del; // struct a2j_port*
    jack_ringbuffer_t* outbound_events; // struct a2j_delivery_event
    jack_nframes_t cycle_start;
    
    sem_t output_semaphore;

    struct a2j_stream stream[2];

} alsa_midi_driver_t;

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

void a2j_error (const char* fmt, ...);

#define A2J_DEBUG
/*#undef A2J_DEBUG*/

#ifdef A2J_DEBUG
extern bool a2j_do_debug;
extern void _a2j_debug (const char* fmt, ...);
#define a2j_debug(fmt, ...) if (a2j_do_debug) { _a2j_debug ((fmt), ##__VA_ARGS__); }
#else
#define a2j_debug(fmt,...)
#endif

#endif /* __jack_alsa_midi_h__ */
