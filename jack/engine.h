/*
    Copyright (C) 2001-2003 Paul Davis
    
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

#define VERBOSE(engine,format,args...) \
	if ((engine)->verbose) fprintf (stderr, format, ## args)

struct _jack_driver;
struct _jack_client_internal;
struct _jack_port_internal;

struct _jack_engine {
    jack_control_t        *control;
    struct _jack_driver   *driver;

    /* these are "callbacks" made by the driver */

    int  (*set_buffer_size)(struct _jack_engine *, jack_nframes_t frames);
    int  (*set_sample_rate)(struct _jack_engine *, jack_nframes_t frames);
    int  (*run_cycle)(struct _jack_engine *, jack_nframes_t nframes,
		      float delayed_usecs);
    void (*transport_cycle_start)(struct _jack_engine *, jack_time_t time);

    /* "private" sections starts here */

    pthread_mutex_t client_lock;
    pthread_mutex_t port_lock;
    pthread_mutex_t request_lock;
    int process_errors;
    int period_msecs;
    int client_timeout_msecs;  /* Time to wait for clients in msecs. Used when jackd is 
				* run in non-ASIO mode and without realtime priority enabled.
				*/
    unsigned int port_max;
    shm_name_t control_shm_name;
    size_t     control_size;
    shm_name_t port_segment_name; /* XXX fix me */
    size_t port_segment_size; /* XXX fix me */
    void *port_segment_address; /* XXX fix me */
    pthread_t main_thread;
    pthread_t server_thread;
    
    /* these lists are all protected by `client_lock' */

    JSList *clients;
    JSList *clients_waiting;

    struct _jack_port_internal *internal_ports;

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
    int watchdog_check;

    struct _jack_client_internal *current_client;

#define JACK_ENGINE_ROLLING_COUNT 32
#define JACK_ENGINE_ROLLING_INTERVAL 1024

    jack_time_t rolling_client_usecs[JACK_ENGINE_ROLLING_COUNT];
    int   rolling_client_usecs_cnt;
    int   rolling_client_usecs_index;
    int   rolling_interval;
    float spare_usecs;
    float usecs_per_cycle;
    
#if defined(__APPLE__) && defined(__POWERPC__) 
    /* specific ressources for server/client real-time thread communication */
    mach_port_t servertask, bp;
    int portnum;
#endif
   
};

/* public functions */

jack_engine_t  *jack_engine_new (int real_time, int real_time_priority, int verbose, int client_timeout);
int             jack_engine_delete (jack_engine_t *);
int             jack_run (jack_engine_t *engine);
int             jack_wait (jack_engine_t *engine);
int             jack_engine_load_driver (jack_engine_t *, int, char **);
void            jack_set_asio_mode (jack_engine_t *, int yn);
void            jack_dump_configuration(jack_engine_t *engine, int take_lock);

extern jack_client_internal_t *
jack_client_internal_by_id (jack_engine_t *engine, jack_client_id_t id);

static inline void jack_lock_graph (jack_engine_t* engine) {
	DEBUG ("acquiring graph lock");
	pthread_mutex_lock (&engine->client_lock);
}

static inline int jack_try_lock_graph (jack_engine_t *engine)
{
	DEBUG ("TRYING to acquiring graph lock");
	return pthread_mutex_trylock (&engine->client_lock);
}

static inline void jack_unlock_graph (jack_engine_t* engine) 
{
	DEBUG ("releasing graph lock");
	pthread_mutex_unlock (&engine->client_lock);
}

#endif /* __jack_engine_h__ */
