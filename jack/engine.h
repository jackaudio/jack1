/* -*- mode: c; c-file-style: "bsd"; -*- */
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
#include <jack/driver_interface.h>

#define VERBOSE(engine,format,args...) \
	if ((engine)->verbose) fprintf (stderr, format, ## args)

struct _jack_driver;
struct _jack_client_internal;
struct _jack_port_internal;

/* Structures is allocated by the engine in local memory to keep track
 * of port buffers and connections. 
 */
typedef struct {
    jack_shm_info_t* shm_info;
    jack_shmsize_t   offset;
} jack_port_buffer_info_t;

/* The engine keeps an array of these in its local memory. */
typedef struct _jack_port_internal {
    struct _jack_port_shared *shared;
    JSList                   *connections;
    jack_port_buffer_info_t  *buffer_info;
} jack_port_internal_t;

/* The engine's internal port type structure. */
typedef struct _jack_port_buffer_list {
    pthread_mutex_t          lock;	/* only lock within server */
    JSList	            *freelist;	/* list of free buffers */
    jack_port_buffer_info_t *info;	/* jack_buffer_info_t array */
} jack_port_buffer_list_t;

/* The main engine structure in local memory. */
struct _jack_engine {
    jack_control_t        *control;

    JSList                *drivers;
    struct _jack_driver   *driver;
    jack_driver_desc_t    *driver_desc;
    JSList                *driver_params;

    /* these are "callbacks" made by the driver */
    int  (*set_buffer_size) (struct _jack_engine *, jack_nframes_t frames);
    int  (*set_sample_rate) (struct _jack_engine *, jack_nframes_t frames);
    int  (*run_cycle)	    (struct _jack_engine *, jack_nframes_t nframes,
			     float delayed_usecs);
    void (*delay)	    (struct _jack_engine *);
    void (*transport_cycle_start) (struct _jack_engine *, jack_time_t time);
    void (*driver_exit)     (struct _jack_engine *);

    /* "private" sections starts here */

    /* engine serialization -- use precedence for deadlock avoidance */
    pthread_mutex_t request_lock;	/* precedes client_lock */
    pthread_mutex_t client_lock;
    pthread_mutex_t port_lock;
    int		    process_errors;
    int		    period_msecs;

    /* Time to wait for clients in msecs.  Used when jackd is run
     * without realtime priority enabled. */
    int		    client_timeout_msecs;

    /* info on the shm segment containing this->control */

    jack_shm_info_t control_shm;

    /* address-space local port buffer and segment info, 
       indexed by the port type_id 
    */
    jack_port_buffer_list_t port_buffers[JACK_MAX_PORT_TYPES];
    jack_shm_info_t         port_segment[JACK_MAX_PORT_TYPES];

    unsigned int    port_max;
    pthread_t	    server_thread;
    pthread_t	    watchdog_thread;

    int		    fds[2];
    jack_client_id_t next_client_id;
    size_t	    pfd_size;
    size_t	    pfd_max;
    struct pollfd  *pfd;
    char	    fifo_prefix[PATH_MAX+1];
    int		   *fifo;
    unsigned long   fifo_size;
    unsigned long   external_client_cnt;
    int		    rtpriority;
    char	    freewheeling;
    char	    verbose;
    char	    temporary;
    int		    reordered;
    int		    watchdog_check;
    pid_t           wait_pid;
    pthread_t       freewheel_thread;

    /* these lists are protected by `client_lock' */
    JSList	   *clients;
    JSList	   *clients_waiting;

    jack_port_internal_t    *internal_ports;
    jack_client_internal_t  *timebase_client;
    jack_port_buffer_info_t *silent_buffer;
    jack_client_internal_t  *current_client;

#define JACK_ENGINE_ROLLING_COUNT 32
#define JACK_ENGINE_ROLLING_INTERVAL 1024

    jack_time_t rolling_client_usecs[JACK_ENGINE_ROLLING_COUNT];
    int		    rolling_client_usecs_cnt;
    int		    rolling_client_usecs_index;
    int		    rolling_interval;
    float	    max_usecs;
    float	    spare_usecs;
    float	    usecs_per_cycle;
    
#ifdef JACK_USE_MACH_THREADS
    /* specific resources for server/client real-time thread communication */
    mach_port_t servertask, bp;
    int portnum;
#endif
   
};

/* public functions */

jack_engine_t  *jack_engine_new (int real_time, int real_time_priority,
				 int do_mlock, int temporary,
				 int verbose, int client_timeout,
				 unsigned int port_max,
                                 pid_t waitpid, JSList *drivers);
void		jack_engine_delete (jack_engine_t *);
int		jack_run (jack_engine_t *engine);
int		jack_wait (jack_engine_t *engine);
int		jack_engine_load_driver (jack_engine_t *engine,
					 jack_driver_desc_t * driver_desc,
					 JSList * driver_params);
void		jack_dump_configuration(jack_engine_t *engine, int take_lock);

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

static inline unsigned int jack_power_of_two (unsigned int n)
{
	return !(n & (n - 1));
}

#endif /* __jack_engine_h__ */
