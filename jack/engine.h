/*
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

    $Id$
*/

#ifndef __jack_engine_h__
#define __jack_engine_h__

#include <jack/jack.h>
#include <jack/internal.h>

struct _jack_driver;
struct _jack_client_internal;
struct _jack_port_internal;

struct _jack_engine {
    jack_control_t        *control;
    struct _jack_driver   *driver;
    int  (*process)(struct _jack_engine *, nframes_t frames);
    int  (*set_buffer_size)(struct _jack_engine *, nframes_t frames);
    int  (*set_sample_rate)(struct _jack_engine *, nframes_t frames);
    int  (*process_lock)(struct _jack_engine *);
    void (*process_unlock)(struct _jack_engine *);
    int  (*post_process)(struct _jack_engine *);
    pthread_mutex_t client_lock;
    pthread_mutex_t cleanup_lock;
    pthread_mutex_t buffer_lock;
    pthread_mutex_t port_lock;
    int process_errors;
    int period_msecs;
    int port_max;
    int control_shm_id;
    key_t control_key;
    key_t port_segment_key; /* XXX fix me */
    void *port_segment_address; /* XXX fix me */
    pthread_t main_thread;
    pthread_t server_thread;
    
    /* these lists are protected by `buffer_lock' */

    GSList *port_segments;
    GSList *port_buffer_freelist;

    /* these lists are all protected by `client_lock' */

    GSList *clients;
    GSList *clients_waiting;
    GSList *connections;

    struct _jack_port_internal *internal_ports;

    GSList *port_types; /* holds ptrs to jack_port_type_info_t */

    int fds[2];
    jack_client_id_t next_client_id;
    size_t pfd_size;
    size_t pfd_max;
    struct pollfd *pfd;
    struct _jack_client_internal *timebase_client;
    jack_port_buffer_info_t *silent_buffer;
    char fifo_prefix[PATH_MAX+1];
    int *fifo;
    unsigned long fifo_size;
    unsigned long external_client_cnt;
    int rtpriority;
    char verbose;
    char asio_mode;
    int reordered;
};

/* public functions */

jack_engine_t  *jack_engine_new (int real_time, int real_time_priority, int verbose);
int             jack_engine_delete (jack_engine_t *);
int             jack_run (jack_engine_t *engine);
int             jack_wait (jack_engine_t *engine);
int             jack_use_driver (jack_engine_t *, struct _jack_driver *);
void            jack_set_temp_dir (const char *);
void            jack_set_asio_mode (jack_engine_t *, int yn);

void            jack_dump_configuration(jack_engine_t *engine, int take_lock);

#endif /* __jack_engine_h__ */
