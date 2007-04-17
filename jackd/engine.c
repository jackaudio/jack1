/* -*- mode: c; c-file-style: "bsd"; -*- */
/*
    Copyright (C) 2001-2003 Paul Davis
    Copyright (C) 2004 Jack O'Quin
    
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

#include <config.h>

#include <math.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <dirent.h>
#include <signal.h>
#include <sys/types.h>
#include <string.h>
#include <limits.h>

#include <jack/internal.h>
#include <jack/engine.h>
#include <jack/messagebuffer.h>
#include <jack/driver.h>
#include <jack/shm.h>
#include <jack/thread.h>
#include <sysdeps/poll.h>
#include <sysdeps/ipc.h>

#ifdef USE_MLOCK
#include <sys/mman.h>
#endif /* USE_MLOCK */

#ifdef USE_CAPABILITIES
/* capgetp and capsetp are linux only extensions, not posix */
#undef _POSIX_SOURCE
#include <sys/capability.h>
#endif

#include "clientengine.h"
#include "transengine.h"

typedef struct {

    jack_port_internal_t *source;
    jack_port_internal_t *destination;
    signed int dir; /* -1 = feedback, 0 = self, 1 = forward */
    jack_client_internal_t *srcclient;
    jack_client_internal_t *dstclient;
} jack_connection_internal_t;

typedef struct _jack_driver_info {
    jack_driver_t *(*initialize)(jack_client_t*, const JSList *);
    void           (*finish);
    char           (*client_name);
    dlhandle       handle;
} jack_driver_info_t;

jack_timer_type_t clock_source = JACK_TIMER_SYSTEM_CLOCK;

static int                    jack_port_assign_buffer (jack_engine_t *,
						       jack_port_internal_t *);
static jack_port_internal_t *jack_get_port_by_name (jack_engine_t *,
						    const char *name);
static int  jack_rechain_graph (jack_engine_t *engine);
static void jack_clear_fifos (jack_engine_t *engine);
static int  jack_port_do_connect (jack_engine_t *engine,
				  const char *source_port,
				  const char *destination_port);
static int  jack_port_do_disconnect (jack_engine_t *engine,
				     const char *source_port,
				     const char *destination_port);
static int  jack_port_do_disconnect_all (jack_engine_t *engine,
					 jack_port_id_t);
static int  jack_port_do_unregister (jack_engine_t *engine, jack_request_t *);
static int  jack_port_do_register (jack_engine_t *engine, jack_request_t *);
static int  jack_do_get_port_connections (jack_engine_t *engine,
					  jack_request_t *req, int reply_fd);
static int  jack_port_disconnect_internal (jack_engine_t *engine,
					   jack_port_internal_t *src, 
					   jack_port_internal_t *dst);
static int  jack_send_connection_notification (jack_engine_t *,
					       jack_client_id_t,
					       jack_port_id_t,
					       jack_port_id_t, int);
static int  jack_deliver_event (jack_engine_t *, jack_client_internal_t *,
				jack_event_t *);
static void jack_deliver_event_to_all (jack_engine_t *engine,
				       jack_event_t *event);
static void jack_engine_post_process (jack_engine_t *);
static int  jack_use_driver (jack_engine_t *engine, jack_driver_t *driver);
static int  jack_run_cycle (jack_engine_t *engine, jack_nframes_t nframes,
			    float delayed_usecs);
static void jack_engine_delay (jack_engine_t *engine,
			       float delayed_usecs);
static void jack_engine_driver_exit (jack_engine_t* engine);
static int  jack_start_freewheeling (jack_engine_t* engine);
static int  jack_stop_freewheeling (jack_engine_t* engine);
static int jack_client_feeds_transitive (jack_client_internal_t *source,
					 jack_client_internal_t *dest);
static int jack_client_sort (jack_client_internal_t *a,
			     jack_client_internal_t *b);
static void jack_check_acyclic (jack_engine_t* engine);
static void jack_compute_all_port_total_latencies (jack_engine_t *engine);


static inline int 
jack_rolling_interval (jack_time_t period_usecs)
{
	return floor ((JACK_ENGINE_ROLLING_INTERVAL * 1000.0f) / period_usecs);
}

void
jack_engine_reset_rolling_usecs (jack_engine_t *engine)
{
	memset (engine->rolling_client_usecs, 0,
		sizeof (engine->rolling_client_usecs));
	engine->rolling_client_usecs_index = 0;
	engine->rolling_client_usecs_cnt = 0;

	if (engine->driver) {
		engine->rolling_interval =
			jack_rolling_interval (engine->driver->period_usecs);
	} else {
		engine->rolling_interval = JACK_ENGINE_ROLLING_INTERVAL;
	}

	engine->spare_usecs = 0;
}

static inline jack_port_type_info_t *
jack_port_type_info (jack_engine_t *engine, jack_port_internal_t *port)
{
	/* Returns a pointer to the port type information in the
	   engine's shared control structure. 
	*/
	return &engine->control->port_types[port->shared->ptype_id];
}

static inline jack_port_buffer_list_t *
jack_port_buffer_list (jack_engine_t *engine, jack_port_internal_t *port)
{
	/* Points to the engine's private port buffer list struct. */
	return &engine->port_buffers[port->shared->ptype_id];
}

static int
make_directory (const char *path)
{
	struct stat statbuf;

	if (stat (path, &statbuf)) {

		if (errno == ENOENT) {
			int mode;

			if (getenv ("JACK_PROMISCUOUS_SERVER")) {
				mode = 0777;
			} else {
				mode = 0700;
			}

			if (mkdir (path, mode) < 0){
				jack_error ("cannot create %s directory (%s)\n",
					    path, strerror (errno));
				return -1;
			}
		} else {
			jack_error ("cannot stat() %s\n", path);
			return -1;
		}

	} else {

		if (!S_ISDIR (statbuf.st_mode)) {
			jack_error ("%s already exists, but is not"
				    " a directory!\n", path);
			return -1;
		}
	}

	return 0;
}

static int
make_socket_subdirectories (const char *server_name)
{
	struct stat statbuf;
        char server_dir[PATH_MAX+1] = "";

	/* check tmpdir directory */
	if (stat (jack_tmpdir, &statbuf)) {
		jack_error ("cannot stat() %s (%s)\n",
			    jack_tmpdir, strerror (errno));
		return -1;
	} else {
		if (!S_ISDIR(statbuf.st_mode)) {
			jack_error ("%s exists, but is not a directory!\n",
				    jack_tmpdir);
			return -1;
		}
	}

	/* create user subdirectory */
	if (make_directory (jack_user_dir ()) < 0) {
		return -1;
	}

	/* create server_name subdirectory */
	if (make_directory (jack_server_dir (server_name, server_dir)) < 0) {
		return -1;
	}

	return 0;
}

static int
make_sockets (const char *server_name, int fd[2])
{
	struct sockaddr_un addr;
	int i;
        char server_dir[PATH_MAX+1] = "";

	if (make_socket_subdirectories (server_name) < 0) {
		return -1;
	}

	/* First, the master server socket */

	if ((fd[0] = socket (AF_UNIX, SOCK_STREAM, 0)) < 0) {
		jack_error ("cannot create server socket (%s)",
			    strerror (errno));
		return -1;
	}

	addr.sun_family = AF_UNIX;
	for (i = 0; i < 999; i++) {
		snprintf (addr.sun_path, sizeof (addr.sun_path) - 1,
			  "%s/jack_%d", jack_server_dir (server_name, server_dir), i);
		if (access (addr.sun_path, F_OK) != 0) {
			break;
		}
	}

	if (i == 999) {
		jack_error ("all possible server socket names in use!!!");
		close (fd[0]);
		return -1;
	}

	if (bind (fd[0], (struct sockaddr *) &addr, sizeof (addr)) < 0) {
		jack_error ("cannot bind server to socket (%s)",
			    strerror (errno));
		close (fd[0]);
		return -1;
	}

	if (listen (fd[0], 1) < 0) {
		jack_error ("cannot enable listen on server socket (%s)",
			    strerror (errno));
		close (fd[0]);
		return -1;
	}

	/* Now the client/server event ack server socket */

	if ((fd[1] = socket (AF_UNIX, SOCK_STREAM, 0)) < 0) {
		jack_error ("cannot create event ACK socket (%s)",
			    strerror (errno));
		close (fd[0]);
		return -1;
	}

	addr.sun_family = AF_UNIX;
	for (i = 0; i < 999; i++) {
		snprintf (addr.sun_path, sizeof (addr.sun_path) - 1,
			  "%s/jack_ack_%d", jack_server_dir (server_name, server_dir), i);
		if (access (addr.sun_path, F_OK) != 0) {
			break;
		}
	}

	if (i == 999) {
		jack_error ("all possible server ACK socket names in use!!!");
		close (fd[0]);
		close (fd[1]);
		return -1;
	}

	if (bind (fd[1], (struct sockaddr *) &addr, sizeof (addr)) < 0) {
		jack_error ("cannot bind server to socket (%s)",
			    strerror (errno));
		close (fd[0]);
		close (fd[1]);
		return -1;
	}

	if (listen (fd[1], 1) < 0) {
		jack_error ("cannot enable listen on server socket (%s)",
			    strerror (errno));
		close (fd[0]);
		close (fd[1]);
		return -1;
	}

	return 0;
}

void
jack_engine_place_port_buffers (jack_engine_t* engine, 
				jack_port_type_id_t ptid,
				jack_shmsize_t one_buffer,
				jack_shmsize_t size,
				unsigned long nports)
{
	jack_shmsize_t offset;		/* shared memory offset */
	jack_port_buffer_info_t *bi;
	jack_port_buffer_list_t* pti = &engine->port_buffers[ptid];
	jack_port_functions_t *pfuncs = jack_get_port_functions(ptid);

	pthread_mutex_lock (&pti->lock);
	offset = 0;
	
	if (pti->info) {

		/* Buffer info array already allocated for this port
		 * type.  This must be a resize operation, so
		 * recompute the buffer offsets, but leave the free
		 * list alone.
		 */
		int i;

		bi = pti->info;
		while (offset < size) {
			bi->offset = offset;
			offset += one_buffer;
			++bi;
		}

		/* update any existing output port offsets */
		for (i = 0; i < engine->port_max; i++) {
			jack_port_shared_t *port = &engine->control->ports[i];
			if (port->in_use &&
			    (port->flags & JackPortIsOutput) &&
			    port->ptype_id == ptid) {
				bi = engine->internal_ports[i].buffer_info;
				if (bi) {
					port->offset = bi->offset;
				}
			}
		}

	} else {
		jack_port_type_info_t* port_type = &engine->control->port_types[ptid];

		/* Allocate an array of buffer info structures for all
		 * the buffers in the segment.  Chain them to the free
		 * list in memory address order, offset zero must come
		 * first.
		 */
		bi = pti->info = (jack_port_buffer_info_t *)
			malloc (nports * sizeof (jack_port_buffer_info_t));

		while (offset < size) {
			bi->offset = offset;
			pti->freelist = jack_slist_append (pti->freelist, bi);
			offset += one_buffer;
			++bi;
		}

		/* Allocate the first buffer of the port segment
		 * for an empy buffer area.
		 * NOTE: audio buffer is zeroed in its buffer_init function.
		 */
		bi = (jack_port_buffer_info_t *) pti->freelist->data;
		pti->freelist = jack_slist_remove_link (pti->freelist,
							pti->freelist);
		port_type->zero_buffer_offset = bi->offset;
		if (ptid == JACK_AUDIO_PORT_TYPE)
			engine->silent_buffer = bi;
	}
	/* initialize buffers */
	{
		int i;
		jack_shm_info_t *shm_info = &engine->port_segment[ptid];
		char* shm_segment = (char *) jack_shm_addr(shm_info);

		bi = pti->info;
		for (i=0; i<nports; ++i, ++bi)
			pfuncs->buffer_init(shm_segment + bi->offset, one_buffer);
	}

	pthread_mutex_unlock (&pti->lock);
}

// JOQ: this should have a return code...
static void
jack_resize_port_segment (jack_engine_t *engine,
			  jack_port_type_id_t ptid,
			  unsigned long nports)
{
	jack_event_t event;
	jack_shmsize_t one_buffer;	/* size of one buffer */
	jack_shmsize_t size;		/* segment size */
	jack_port_type_info_t* port_type = &engine->control->port_types[ptid];
	jack_shm_info_t* shm_info = &engine->port_segment[ptid];

	if (port_type->buffer_scale_factor < 0) {
		one_buffer = port_type->buffer_size;
	} else {
		one_buffer = sizeof (jack_default_audio_sample_t)
			* port_type->buffer_scale_factor
			* engine->control->buffer_size;
	}

	size = nports * one_buffer;

	if (shm_info->attached_at == 0) {

		if (jack_shmalloc (size, shm_info)) {
			jack_error ("cannot create new port segment of %d"
				    " bytes (%s)", 
				    size,
				    strerror (errno));
			return;
		}
		
		if (jack_attach_shm (shm_info)) {
			jack_error ("cannot attach to new port segment "
				    "(%s)", strerror (errno));
			return;
		}

		engine->control->port_types[ptid].shm_registry_index =
			shm_info->index;

	} else {

		/* resize existing buffer segment */
		if (jack_resize_shm (shm_info, size)) {
			jack_error ("cannot resize port segment to %d bytes,"
				    " (%s)", size,
				    strerror (errno));
			return;
		}
	}

	jack_engine_place_port_buffers (engine, ptid, one_buffer, size, nports);

#ifdef USE_MLOCK
	if (engine->control->real_time) {

	/* Although we've called mlockall(CURRENT|FUTURE), the
		 * Linux VM manager still allows newly allocated pages
		 * to fault on first reference.  This mlock() ensures
		 * that any new pages are present before restarting
		 * the process cycle.  Since memory locks do not
		 * stack, they can still be unlocked with a single
		 * munlockall().
		 */

		int rc = mlock (jack_shm_addr (shm_info), size);
		if (rc < 0) {
			jack_error("JACK: unable to mlock() port buffers: "
				   "%s", strerror(errno));
		}
	}
#endif /* USE_MLOCK */

	/* Tell everybody about this segment. */
	event.type = AttachPortSegment;
	event.y.ptid = ptid;
	jack_deliver_event_to_all (engine, &event);
}

/* The driver invokes this callback both initially and whenever its
 * buffer size changes. 
 */
static int
jack_driver_buffer_size (jack_engine_t *engine, jack_nframes_t nframes)
{
	int i;
	jack_event_t event;
	JSList *node;

	VERBOSE (engine, "new buffer size %" PRIu32 "\n", nframes);

	engine->control->buffer_size = nframes;
	if (engine->driver)
		engine->rolling_interval =
			jack_rolling_interval (engine->driver->period_usecs);

	for (i = 0; i < engine->control->n_port_types; ++i) {
		jack_resize_port_segment (engine, i, engine->control->port_max);
	}

	/* update shared client copy of nframes */
	jack_lock_graph (engine);
	for (node = engine->clients; node; node = jack_slist_next (node)) {
		jack_client_internal_t *client = node->data;
		client->control->nframes = nframes;
	}
	jack_unlock_graph (engine);

	event.type = BufferSizeChange;
	jack_deliver_event_to_all (engine, &event);

	return 0;
}

/* handle client SetBufferSize request */
int
jack_set_buffer_size_request (jack_engine_t *engine, jack_nframes_t nframes)
{
	/* precondition: caller holds the request_lock */
	int rc;
	jack_driver_t* driver = engine->driver;

	if (driver == NULL)
		return ENXIO;		/* no such device */

	if (!jack_power_of_two(nframes)) {
  		jack_error("buffer size %" PRIu32 " not a power of 2",
			   nframes);
		return EINVAL;
	}

	rc = driver->bufsize(driver, nframes);
	if (rc != 0)
		jack_error("driver does not support %" PRIu32
			   "-frame buffers", nframes);

	return rc;
}


static JSList * 
jack_process_internal(jack_engine_t *engine, JSList *node,
		      jack_nframes_t nframes)
{
	jack_client_internal_t *client;
	jack_client_control_t *ctl;
	
	client = (jack_client_internal_t *) node->data;
	ctl = client->control;
	
	/* internal client */

	DEBUG ("invoking an internal client's callbacks");
	ctl->state = Running;
	engine->current_client = client;

	/* XXX how to time out an internal client? */

	if (ctl->sync_cb)
		jack_call_sync_client (ctl->private_client);

	if (ctl->process)
		if (ctl->process (nframes, ctl->process_arg)) {
			jack_error ("internal client %s failed", ctl->name);
			engine->process_errors++;
		}

	if (ctl->timebase_cb)
		jack_call_timebase_master (ctl->private_client);
		
	ctl->state = Finished;

	if (engine->process_errors)
		return NULL;		/* will stop the loop */
	else
		return jack_slist_next (node);
}

#ifdef JACK_USE_MACH_THREADS
static JSList * 
jack_process_external(jack_engine_t *engine, JSList *node)
{
        jack_client_internal_t * client = (jack_client_internal_t *) node->data;
        jack_client_control_t *ctl;
        
        client = (jack_client_internal_t *) node->data;
        ctl = client->control;
        
        engine->current_client = client;

	// a race exists if we do this after the write(2) 
        ctl->state = Triggered; 
        ctl->signalled_at = jack_get_microseconds();
        ctl->awake_at = 0;
        ctl->finished_at = 0;
        
        if (jack_client_resume(client) < 0) {
            jack_error("Client will be removed\n");
            ctl->state = Finished;
        }
        
        return jack_slist_next (node);
}
#else /* !JACK_USE_MACH_THREADS */
static JSList * 
jack_process_external(jack_engine_t *engine, JSList *node)
{
	int status = 0;
	char c = 0;
	struct pollfd pfd[1];
	int poll_timeout;
	jack_client_internal_t *client;
	jack_client_control_t *ctl;
	jack_time_t now, then;

	client = (jack_client_internal_t *) node->data;
	
	ctl = client->control;

	/* external subgraph */

	/* a race exists if we do this after the write(2) */
	ctl->state = Triggered; 

	ctl->signalled_at = jack_get_microseconds();
	ctl->awake_at = 0;
	ctl->finished_at = 0;

	engine->current_client = client;

	DEBUG ("calling process() on an external subgraph, fd==%d",
	       client->subgraph_start_fd);

	if (write (client->subgraph_start_fd, &c, sizeof (c)) != sizeof (c)) {
		jack_error ("cannot initiate graph processing (%s)",
			    strerror (errno));
		engine->process_errors++;
		return NULL; /* will stop the loop */
	} 

	then = jack_get_microseconds ();

	if (engine->freewheeling) {
		poll_timeout = 10000; /* 10 seconds */
	} else {
		poll_timeout = (engine->client_timeout_msecs > 0 ?
				engine->client_timeout_msecs :
				1 + engine->driver->period_usecs/1000);
	}

	pfd[0].fd = client->subgraph_wait_fd;
	pfd[0].events = POLLERR|POLLIN|POLLHUP|POLLNVAL;

	DEBUG ("waiting on fd==%d for process() subgraph to finish",
	       client->subgraph_wait_fd);

	if (poll (pfd, 1, poll_timeout) < 0) {
		jack_error ("poll on subgraph processing failed (%s)",
			    strerror (errno));
		status = -1; 
	}

	DEBUG ("\n\n\n\n\n back from subgraph poll, revents = 0x%x\n\n\n", pfd[0].revents);

	if (pfd[0].revents & ~POLLIN) {
		jack_error ("subgraph starting at %s lost client",
			    client->control->name);
		status = -2; 
	}

	if (pfd[0].revents & POLLIN) {
		status = 0;
	} else {
		jack_error ("subgraph starting at %s timed out "
			    "(subgraph_wait_fd=%d, status = %d, state = %s)", 
			    client->control->name,
			    client->subgraph_wait_fd, status, 
			    jack_client_state_name (client));
		status = 1;
	}

	now = jack_get_microseconds ();

	if (status != 0) {
		VERBOSE (engine, "at %" PRIu64
			 " waiting on %d for %" PRIu64
			 " usecs, status = %d sig = %" PRIu64
			 " awa = %" PRIu64 " fin = %" PRIu64
			 " dur=%" PRIu64 "\n",
			 now,
			 client->subgraph_wait_fd,
			 now - then,
			 status,
			 ctl->signalled_at,
			 ctl->awake_at,
			 ctl->finished_at,
			 ctl->finished_at? (ctl->finished_at -
					    ctl->signalled_at): 0);

		/* we can only consider the timeout a client error if
		 * it actually woke up.  its possible that the kernel
		 * scheduler screwed us up and never woke up the
		 * client in time. sigh.
		 */
		if (ctl->awake_at > 0) {
			ctl->timed_out++;
		}

		engine->process_errors++;
		return NULL;		/* will stop the loop */

	} else {

		DEBUG ("reading byte from subgraph_wait_fd==%d",
		       client->subgraph_wait_fd);

		if (read (client->subgraph_wait_fd, &c, sizeof(c))
		    != sizeof (c)) {
			jack_error ("pp: cannot clean up byte from graph wait "
				    "fd (%s)", strerror (errno));
			client->error++;
			return NULL;	/* will stop the loop */
		}
	}

	/* Move to next internal client (or end of client list) */
	while (node) {
		if (jack_client_is_internal ((jack_client_internal_t *)
					     node->data)) {
			break;
		}
		node = jack_slist_next (node);
	}
	
	return node;
}

#endif /* JACK_USE_MACH_THREADS */


static int
jack_engine_process (jack_engine_t *engine, jack_nframes_t nframes)
{
	/* precondition: caller has graph_lock */
	jack_client_internal_t *client;
	JSList *node;

	engine->process_errors = 0;
	engine->watchdog_check = 1;

	for (node = engine->clients; node; node = jack_slist_next (node)) {
		jack_client_control_t *ctl =
			((jack_client_internal_t *) node->data)->control;
		ctl->state = NotTriggered;
		ctl->nframes = nframes;
		ctl->timed_out = 0;
	}

	for (node = engine->clients; engine->process_errors == 0 && node; ) {

		client = (jack_client_internal_t *) node->data;
		
		DEBUG ("considering client %s for processing",
		       client->control->name);

		if (!client->control->active || client->control->dead) {
			node = jack_slist_next (node);
		} else if (jack_client_is_internal (client)) {
			node = jack_process_internal (engine, node, nframes);
		} else {
			node = jack_process_external (engine, node);
		}
	}

	return engine->process_errors > 0;
}

static void 
jack_calc_cpu_load(jack_engine_t *engine)
{
	jack_time_t cycle_end = jack_get_microseconds ();
	
	/* store the execution time for later averaging */

	engine->rolling_client_usecs[engine->rolling_client_usecs_index++] = 
		cycle_end - engine->control->current_time.usecs;

	//printf ("cycle_end - engine->control->current_time.usecs %ld\n",
	//	(long) (cycle_end - engine->control->current_time.usecs));

	if (engine->rolling_client_usecs_index >= JACK_ENGINE_ROLLING_COUNT) {
		engine->rolling_client_usecs_index = 0;
	}

	/* every so often, recompute the current maximum use over the
	   last JACK_ENGINE_ROLLING_COUNT client iterations.
	*/

	if (++engine->rolling_client_usecs_cnt
	    % engine->rolling_interval == 0) {
		float max_usecs = 0.0f;
		int i;

		for (i = 0; i < JACK_ENGINE_ROLLING_COUNT; i++) {
			if (engine->rolling_client_usecs[i] > max_usecs) {
				max_usecs = engine->rolling_client_usecs[i];
			}
		}

		if (max_usecs > engine->max_usecs) {
			engine->max_usecs = max_usecs;
		}

		if (max_usecs < engine->driver->period_usecs) {
			engine->spare_usecs =
				engine->driver->period_usecs - max_usecs;
		} else {
			engine->spare_usecs = 0;
		}

		engine->control->cpu_load =
			(1.0f - (engine->spare_usecs /
				 engine->driver->period_usecs)) * 50.0f
			+ (engine->control->cpu_load * 0.5f);

		VERBOSE (engine, "load = %.4f max usecs: %.3f, "
			 "spare = %.3f\n", engine->control->cpu_load,
			 max_usecs, engine->spare_usecs);
	}

}

static void
jack_engine_post_process (jack_engine_t *engine)
{
	/* precondition: caller holds the graph lock. */
	jack_client_control_t *ctl;
	jack_client_internal_t *client;
	JSList *node;
	int need_remove = FALSE;

	jack_transport_cycle_end (engine);
	
	/* find any clients that need removal due to timeouts, etc. */
	for (node = engine->clients; node; node = jack_slist_next (node) ) {

		client = (jack_client_internal_t *) node->data;
		ctl = client->control;

		/* this check is invalid for internal clients and
		   external clients with no process callback.
		*/
		if (!jack_client_is_internal (client) && ctl->process) {
			if (ctl->awake_at != 0 &&
			    ctl->state > NotTriggered &&
			    ctl->state != Finished &&
			    ctl->timed_out++) {
				VERBOSE(engine, "client %s error: awake_at = %"
					 PRIu64
					 " state = %d timed_out = %d\n",
					 ctl->name,
					 ctl->awake_at,
					 ctl->state,
					 ctl->timed_out);
				client->error++;
			}
		}

		if (client->error) {
			need_remove = TRUE;
		}
	}
	
	if (need_remove) {
		jack_remove_clients (engine);
	}

	jack_calc_cpu_load (engine);
}

#ifndef JACK_USE_MACH_THREADS
static void *
jack_watchdog_thread (void *arg)
{
	jack_engine_t *engine = (jack_engine_t *) arg;

	engine->watchdog_check = 0;

	while (1) {
		usleep (1000 * JACKD_WATCHDOG_TIMEOUT);
		if (!engine->freewheeling && engine->watchdog_check == 0) {

			jack_error ("jackd watchdog: timeout - killing jackd");

			/* Kill the current client (guilt by association). */
			if (engine->current_client) {
					kill (engine->current_client->
					      control->pid, SIGKILL);
			}

			/* kill our process group, try to get a dump */
			kill (-getpgrp(), SIGABRT);
			/*NOTREACHED*/
			exit (1);
		}
		engine->watchdog_check = 0;
	}
}

static int
jack_start_watchdog (jack_engine_t *engine)
{
	int watchdog_priority = engine->rtpriority + 10;
	int max_priority = sched_get_priority_max (SCHED_FIFO);

	if ((max_priority != -1) &&
	    (max_priority < watchdog_priority))
		watchdog_priority = max_priority;
	
	if (jack_client_create_thread (NULL, &engine->watchdog_thread, watchdog_priority,
				       TRUE, jack_watchdog_thread, engine)) {
		jack_error ("cannot start watchdog thread");
		return -1;
	}

	return 0;
}
#endif /* !JACK_USE_MACH_THREADS */

static jack_driver_info_t *
jack_load_driver (jack_engine_t *engine, jack_driver_desc_t * driver_desc)
{
	const char *errstr;
	jack_driver_info_t *info;

	info = (jack_driver_info_t *) calloc (1, sizeof (*info));

	info->handle = dlopen (driver_desc->file, RTLD_NOW|RTLD_GLOBAL);
	
	if (info->handle == NULL) {
		if ((errstr = dlerror ()) != 0) {
			jack_error ("can't load \"%s\": %s", driver_desc->file,
				    errstr);
		} else {
			jack_error ("bizarre error loading driver shared "
				    "object %s", driver_desc->file);
		}
		goto fail;
	}

	info->initialize = dlsym (info->handle, "driver_initialize");

	if ((errstr = dlerror ()) != 0) {
		jack_error ("no initialize function in shared object %s\n",
			    driver_desc->file);
		goto fail;
	}

	info->finish = dlsym (info->handle, "driver_finish");

	if ((errstr = dlerror ()) != 0) {
		jack_error ("no finish function in in shared driver object %s",
			    driver_desc->file);
		goto fail;
	}

	info->client_name = (char *) dlsym (info->handle, "driver_client_name");

	if ((errstr = dlerror ()) != 0) {
		jack_error ("no client name in in shared driver object %s",
			    driver_desc->file);
		goto fail;
	}

	return info;

  fail:
	if (info->handle) {
		dlclose (info->handle);
	}
	free (info);
	return NULL;
	
}

void
jack_driver_unload (jack_driver_t *driver)
{
	driver->finish (driver);
	dlclose (driver->handle);
}

int
jack_engine_load_driver (jack_engine_t *engine,
			 jack_driver_desc_t * driver_desc,
			 JSList * driver_params)
{
	jack_client_internal_t *client;
	jack_driver_t *driver;
	jack_driver_info_t *info;

	if ((info = jack_load_driver (engine, driver_desc)) == NULL) {
		return -1;
	}

	if ((client = jack_create_driver_client (engine, info->client_name)
		    ) == NULL) {
		return -1;
	}

	if ((driver = info->initialize (client->control->private_client,
					driver_params)) == NULL) {
		free (info);
		return -1;
	}

	driver->handle = info->handle;
	driver->finish = info->finish;
	driver->internal_client = client;
	free (info);

	if (jack_use_driver (engine, driver)) {
		jack_driver_unload (driver);
		jack_client_delete (engine, client);
		return -1;
	}

	engine->driver_desc   = driver_desc;
	engine->driver_params = driver_params;

	if (engine->control->real_time) {
		/* Stephane Letz : letz@grame.fr Watch dog thread is
		 * not needed on MacOSX since CoreAudio drivers
		 * already contains a similar mechanism.
		 */
#ifndef JACK_USE_MACH_THREADS
		if (jack_start_watchdog (engine)) {
			return -1;
		}
		engine->watchdog_check = 1;
#endif
	}
	return 0;
}

#ifdef USE_CAPABILITIES

static int check_capabilities (jack_engine_t *engine)
{
	cap_t caps = cap_init();
	cap_flag_value_t cap;
	pid_t pid;
	int have_all_caps = 1;

	if (caps == NULL) {
		VERBOSE (engine, "check: could not allocate capability"
			 " working storage\n");
		return 0;
	}
	pid = getpid ();
	cap_clear (caps);
	if (capgetp (pid, caps)) {
		VERBOSE (engine, "check: could not get capabilities "
			 "for process %d\n", pid);
		return 0;
	}
	/* check that we are able to give capabilites to other processes */
	cap_get_flag(caps, CAP_SETPCAP, CAP_EFFECTIVE, &cap);
	if (cap == CAP_CLEAR) {
		have_all_caps = 0;
		goto done;
	}
	/* check that we have the capabilities we want to transfer */
	cap_get_flag(caps, CAP_SYS_NICE, CAP_EFFECTIVE, &cap);
	if (cap == CAP_CLEAR) {
		have_all_caps = 0;
		goto done;
	}
	cap_get_flag(caps, CAP_SYS_RESOURCE, CAP_EFFECTIVE, &cap);
	if (cap == CAP_CLEAR) {
		have_all_caps = 0;
		goto done;
	}
	cap_get_flag(caps, CAP_IPC_LOCK, CAP_EFFECTIVE, &cap);
	if (cap == CAP_CLEAR) {
		have_all_caps = 0;
		goto done;
	}
  done:
	cap_free (caps);
	return have_all_caps;
}


static int give_capabilities (jack_engine_t *engine, pid_t pid)
{
	cap_t caps = cap_init();
	const unsigned caps_size = 3;
	cap_value_t cap_list[] = {CAP_SYS_NICE, CAP_SYS_RESOURCE, CAP_IPC_LOCK};

	if (caps == NULL) {
		VERBOSE (engine, "give: could not allocate capability"
			 " working storage\n");
		return -1;
	}
	cap_clear(caps);
	if (capgetp (pid, caps)) {
		VERBOSE (engine, "give: could not get current "
			 "capabilities for process %d\n", pid);
		cap_clear(caps);
	}
	cap_set_flag(caps, CAP_EFFECTIVE, caps_size, cap_list , CAP_SET);
	cap_set_flag(caps, CAP_INHERITABLE, caps_size, cap_list , CAP_SET);
	cap_set_flag(caps, CAP_PERMITTED, caps_size, cap_list , CAP_SET);
	if (capsetp (pid, caps)) {
		cap_free (caps);
		return -1;
	}
	cap_free (caps);
	return 0;
}

static int
jack_set_client_capabilities (jack_engine_t *engine, pid_t cap_pid)
{
	int ret = -1;

	/* before sending this request the client has
	   already checked that the engine has
	   realtime capabilities, that it is running
	   realtime and that the pid is defined
	*/

	if ((ret = give_capabilities (engine, cap_pid)) != 0) {
		jack_error ("could not give capabilities to "
			    "process %d\n",
			    cap_pid);
	} else {
		VERBOSE (engine, "gave capabilities to"
			 " process %d\n",
			 cap_pid);
	}

	return ret;
}	

#endif /* USE_CAPABILITIES */

/* perform internal or external client request
 *
 * reply_fd is NULL for internal requests
 */
static void
do_request (jack_engine_t *engine, jack_request_t *req, int *reply_fd)
{
	/* The request_lock serializes internal requests (from any
	 * server thread) with external requests (always from the
	 * server thread). */
	pthread_mutex_lock (&engine->request_lock);

	DEBUG ("got a request of type %d", req->type);

	switch (req->type) {
	case RegisterPort:
		req->status = jack_port_do_register (engine, req);
		break;

	case UnRegisterPort:
		req->status = jack_port_do_unregister (engine, req);
		break;

	case ConnectPorts:
		req->status = jack_port_do_connect
			(engine, req->x.connect.source_port,
			 req->x.connect.destination_port);
		break;

	case DisconnectPort:
		req->status = jack_port_do_disconnect_all
			(engine, req->x.port_info.port_id);
		break;

	case DisconnectPorts:
		req->status = jack_port_do_disconnect
			(engine, req->x.connect.source_port,
			 req->x.connect.destination_port);
		break;

	case ActivateClient:
		req->status = jack_client_activate (engine, req->x.client_id);
		break;

	case DeactivateClient:
		req->status = jack_client_deactivate (engine, req->x.client_id);
		break;

	case SetTimeBaseClient:
		req->status = jack_timebase_set (engine,
						 req->x.timebase.client_id,
						 req->x.timebase.conditional);
		break;

	case ResetTimeBaseClient:
		req->status = jack_timebase_reset (engine, req->x.client_id);
		break;

	case SetSyncClient:
		req->status =
			jack_transport_client_set_sync (engine,
							req->x.client_id);
		break;

	case ResetSyncClient:
		req->status =
			jack_transport_client_reset_sync (engine,
							  req->x.client_id);
		break;

	case SetSyncTimeout:
		req->status = jack_transport_set_sync_timeout (engine,
							       req->x.timeout);
		break;

#ifdef USE_CAPABILITIES
	case SetClientCapabilities:
		req->status = jack_set_client_capabilities (engine,
							    req->x.cap_pid);
		break;
#endif /* USE_CAPABILITIES */
		
	case GetPortConnections:
	case GetPortNConnections:
		//JOQ bug: reply_fd may be NULL if internal request
		if ((req->status =
		     jack_do_get_port_connections (engine, req, *reply_fd))
		    == 0) {
			/* we have already replied, don't do it again */
			*reply_fd = -1;
		}
		break;

	case FreeWheel:
		req->status = jack_start_freewheeling (engine);
		break;

	case StopFreeWheel:
		req->status = jack_stop_freewheeling (engine);
		break;

	case SetBufferSize:
		req->status = jack_set_buffer_size_request (engine,
							   req->x.nframes);
		break;

	case IntClientHandle:
		jack_intclient_handle_request (engine, req);
		break;

	case IntClientLoad:
		jack_intclient_load_request (engine, req);
		break;

	case IntClientName:
		jack_intclient_name_request (engine, req);
		break;

	case IntClientUnload:
		jack_intclient_unload_request (engine, req);
		break;

	case RecomputeTotalLatencies:
		jack_lock_graph (engine);
		jack_compute_all_port_total_latencies (engine);
		jack_unlock_graph (engine);
		req->status = 0;
		break;

	default:
		/* some requests are handled entirely on the client
		 * side, by adjusting the shared memory area(s) */
		break;
	}

	pthread_mutex_unlock (&engine->request_lock);

	DEBUG ("status of request: %d", req->status);
}

int
internal_client_request (void* ptr, jack_request_t *request)
{
	do_request ((jack_engine_t*) ptr, request, NULL);
	return request->status;
}

static int
handle_external_client_request (jack_engine_t *engine, int fd)
{
	jack_request_t req;
	jack_client_internal_t *client = 0;
	int reply_fd;
	JSList *node;
	ssize_t r;

	DEBUG ("HIT: before lock");
	
	jack_lock_graph (engine);

	DEBUG ("HIT: before for");
	
	for (node = engine->clients; node; node = jack_slist_next (node)) {
		if (((jack_client_internal_t *) node->data)->request_fd == fd) {
			DEBUG ("HIT: in for");
			client = (jack_client_internal_t *) node->data;
			break;
		}
	}
	DEBUG ("HIT: after for");

	jack_unlock_graph (engine);

	if (client == NULL) {
		jack_error ("client input on unknown fd %d!", fd);
		return -1;
	}

	if ((r = read (client->request_fd, &req, sizeof (req)))
	    < (ssize_t) sizeof (req)) {
		jack_error ("cannot read request from client (%d/%d/%s)",
			    r, sizeof(req), strerror (errno));
		client->error++;
		return -1;
	}

	reply_fd = client->request_fd;
	
	do_request (engine, &req, &reply_fd);
	
	if (reply_fd >= 0) {
		DEBUG ("replying to client");
		if (write (reply_fd, &req, sizeof (req))
		    < (ssize_t) sizeof (req)) {
			jack_error ("cannot write request result to client");
			return -1;
		}
	} else {
		DEBUG ("*not* replying to client");
        }

	return 0;
}

static int
handle_client_ack_connection (jack_engine_t *engine, int client_fd)
{
	jack_client_internal_t *client;
	jack_client_connect_ack_request_t req;
	jack_client_connect_ack_result_t res;

	if (read (client_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot read ACK connection request from client");
		return -1;
	}

	if ((client = jack_client_internal_by_id (engine, req.client_id))
	    == NULL) {
		jack_error ("unknown client ID in ACK connection request");
		return -1;
	}

	client->event_fd = client_fd;

	res.status = 0;

	if (write (client->event_fd, &res, sizeof (res)) != sizeof (res)) {
		jack_error ("cannot write ACK connection response to client");
		return -1;
	}

	return 0;
}

static void *
jack_server_thread (void *arg)

{
	jack_engine_t *engine = (jack_engine_t *) arg;
	struct sockaddr_un client_addr;
	socklen_t client_addrlen;
	struct pollfd *pfd;
	int client_socket;
	int done = 0;
	int i;
	int max;
	
	engine->pfd[0].fd = engine->fds[0];
	engine->pfd[0].events = POLLIN|POLLERR;
	engine->pfd[1].fd = engine->fds[1];
	engine->pfd[1].events = POLLIN|POLLERR;
	engine->pfd_max = 2;
	pfd = engine->pfd;
	max = engine->pfd_max;

	while (!done) {
		DEBUG ("start while");
		
	
		if (poll (pfd, max, 10000) < 0) {
			if (errno == EINTR) {
				continue;
			}
			jack_error ("poll failed (%s)", strerror (errno));
			break;
		}

		DEBUG("server thread back from poll");
		
		/* Stephane Letz: letz@grame.fr : has to be added
		 * otherwise pthread_cancel() does not work on MacOSX */
		pthread_testcancel();
			
		/* check each client socket before handling other request*/
		for (i = 2; i < max; i++) {
			if (pfd[i].fd < 0) {
				continue;
			}

			if (pfd[i].revents & ~POLLIN) {
				jack_client_disconnect (engine, pfd[i].fd);
			} else if (pfd[i].revents & POLLIN) {
				if (handle_external_client_request
				    (engine, pfd[i].fd)) {
					jack_error ("could not handle external"
						    " client request");
#ifdef JACK_USE_MACH_THREADS
                                    /* poll is implemented using
				       select (see the macosx/fakepoll
				       code). When the socket is closed
				       select does not return any error,
				       POLLIN is true and the next read
				       will return 0 bytes. This
				       behaviour is diffrent from the
				       Linux poll behaviour. Thus we use
				       this condition as a socket error
				       and remove the client.
                                    */
                                    jack_client_disconnect(engine, pfd[i].fd);
#endif /* JACK_USE_MACH_THREADS */
				}
			}
		}
		
		/* check the master server socket */

		if (pfd[0].revents & POLLERR) {
			jack_error ("error on server socket");
			break;
		}
	
		if (engine->control->engine_ok && pfd[0].revents & POLLIN) {
			DEBUG ("pfd[0].revents & POLLIN");

			memset (&client_addr, 0, sizeof (client_addr));
			client_addrlen = sizeof (client_addr);

			if ((client_socket =
			     accept (engine->fds[0],
				     (struct sockaddr *) &client_addr,
				     &client_addrlen)) < 0) {
				jack_error ("cannot accept new connection (%s)",
					    strerror (errno));
			} else if (jack_client_create (engine, client_socket) < 0) {
				jack_error ("cannot complete client "
					    "connection process");
				close (client_socket);
			}
		}
		
		/* Possibly, jack_client_create() may have
		 * realloced engine->pfd.  We depend on that being
		 * done within this thread.  That is currently true,
		 * since external clients are only created here. 
		 */
		pfd = engine->pfd;
		max = engine->pfd_max;

		/* check the ACK server socket */

		if (pfd[1].revents & POLLERR) {
			jack_error ("error on server ACK socket");
			break;
		}

		if (engine->control->engine_ok && pfd[1].revents & POLLIN) {
			DEBUG ("pfd[1].revents & POLLIN");

			memset (&client_addr, 0, sizeof (client_addr));
			client_addrlen = sizeof (client_addr);

			if ((client_socket =
			     accept (engine->fds[1],
				     (struct sockaddr *) &client_addr,
				     &client_addrlen)) < 0) {
				jack_error ("cannot accept new ACK connection"
					    " (%s)", strerror (errno));
			} else if (handle_client_ack_connection
				   (engine, client_socket)) {
				jack_error ("cannot complete client ACK "
					    "connection process");
				close (client_socket);
			}
		}
	}

	return 0;
}

jack_engine_t *
jack_engine_new (int realtime, int rtpriority, int do_mlock, int do_unlock,
		 const char *server_name, int temporary, int verbose,
		 int client_timeout, unsigned int port_max, pid_t wait_pid,
		 jack_nframes_t frame_time_offset, JSList *drivers)
{
	jack_engine_t *engine;
	unsigned int i;
        char server_dir[PATH_MAX+1] = "";

#ifdef USE_CAPABILITIES
	uid_t uid = getuid ();
	uid_t euid = geteuid ();
#endif /* USE_CAPABILITIES */

	/* before we start allocating resources, make sure that if realtime was requested that we can 
	   actually do it.
	*/

	if (realtime) {
		if (jack_acquire_real_time_scheduling (pthread_self(), 10) != 0) {
			/* can't run realtime - time to bomb */
			return NULL;
		}

		jack_drop_real_time_scheduling (pthread_self());

#ifdef USE_MLOCK

		if (do_mlock && (mlockall (MCL_CURRENT | MCL_FUTURE) != 0)) {
			jack_error ("cannot lock down memory for jackd (%s)",
				    strerror (errno));
#ifdef ENSURE_MLOCK
			return NULL;
#endif /* ENSURE_MLOCK */
		}
#endif /* USE_MLOCK */
	}

	/* start a thread to display messages from realtime threads */
	jack_messagebuffer_init();

	jack_init_time ();

	engine = (jack_engine_t *) malloc (sizeof (jack_engine_t));

	engine->drivers = drivers;
	engine->driver = NULL;
	engine->driver_desc = NULL;
	engine->driver_params = NULL;

	engine->set_sample_rate = jack_set_sample_rate;
	engine->set_buffer_size = jack_driver_buffer_size;
	engine->run_cycle = jack_run_cycle;
	engine->delay = jack_engine_delay;
	engine->driver_exit = jack_engine_driver_exit;
	engine->transport_cycle_start = jack_transport_cycle_start;
	engine->client_timeout_msecs = client_timeout;

	engine->next_client_id = 1;	/* 0 is a NULL client ID */
	engine->port_max = port_max;
	engine->rtpriority = rtpriority;
	engine->silent_buffer = 0;
	engine->verbose = verbose;
	engine->server_name = server_name;
	engine->temporary = temporary;
	engine->freewheeling = 0;
	engine->feedbackcount = 0;
	engine->wait_pid = wait_pid;

	jack_engine_reset_rolling_usecs (engine);
	engine->max_usecs = 0.0f;

	pthread_mutex_init (&engine->client_lock, 0);
	pthread_mutex_init (&engine->port_lock, 0);
	pthread_mutex_init (&engine->request_lock, 0);

	engine->clients = 0;

	engine->pfd_size = 16;
	engine->pfd_max = 0;
	engine->pfd = (struct pollfd *) malloc (sizeof (struct pollfd)
						* engine->pfd_size);

	engine->fifo_size = 16;
	engine->fifo = (int *) malloc (sizeof (int) * engine->fifo_size);
	for (i = 0; i < engine->fifo_size; i++) {
		engine->fifo[i] = -1;
	}

	engine->external_client_cnt = 0;

	srandom (time ((time_t *) 0));

	if (jack_shmalloc (sizeof (jack_control_t)
			   + ((sizeof (jack_port_shared_t) * engine->port_max)),
			   &engine->control_shm)) {
		jack_error ("cannot create engine control shared memory "
			    "segment (%s)", strerror (errno));
		return NULL;
	}

	if (jack_attach_shm (&engine->control_shm)) {
		jack_error ("cannot attach to engine control shared memory"
			    " (%s)", strerror (errno));
		jack_destroy_shm (&engine->control_shm);
		return NULL;
	}

	engine->control = (jack_control_t *)
		jack_shm_addr (&engine->control_shm);

	/* Setup port type information from builtins. buffer space is
	 * allocated when the driver calls jack_driver_buffer_size().
	 */
	for (i = 0; jack_builtin_port_types[i].type_name[0]; ++i) {

		memcpy (&engine->control->port_types[i],
			&jack_builtin_port_types[i],
			sizeof (jack_port_type_info_t));

		VERBOSE (engine, "registered builtin port type %s\n",
			 engine->control->port_types[i].type_name);

		/* the port type id is index into port_types array */
		engine->control->port_types[i].ptype_id = i;

		/* be sure to initialize mutex correctly */
		pthread_mutex_init (&engine->port_buffers[i].lock, NULL);

		/* set buffer list info correctly */
		engine->port_buffers[i].freelist = NULL;
		engine->port_buffers[i].info = NULL;
		
		/* mark each port segment as not allocated */
		engine->port_segment[i].index = -1;
		engine->port_segment[i].attached_at = 0;
	}

	engine->control->n_port_types = i;

	/* Mark all ports as available */

	for (i = 0; i < engine->port_max; i++) {
		engine->control->ports[i].in_use = 0;
		engine->control->ports[i].id = i;
	}

	/* allocate internal port structures so that we can keep track
	 * of port connections.
	 */
	engine->internal_ports = (jack_port_internal_t *)
		malloc (sizeof (jack_port_internal_t) * engine->port_max);

	for (i = 0; i < engine->port_max; i++) {
		engine->internal_ports[i].connections = 0;
	}

	if (make_sockets (engine->server_name, engine->fds) < 0) {
		jack_error ("cannot create server sockets");
		return NULL;
	}

	engine->control->port_max = engine->port_max;
	engine->control->real_time = realtime;
	engine->control->client_priority = (realtime
					    ? engine->rtpriority - 1
					    : 0);
	engine->control->do_mlock = do_mlock;
	engine->control->do_munlock = do_unlock;
	engine->control->cpu_load = 0;
	engine->control->xrun_delayed_usecs = 0;
	engine->control->max_delayed_usecs = 0;

	jack_set_clock_source (clock_source);
	engine->control->clock_source = clock_source;

	VERBOSE (engine, "clock source = %s\n", jack_clock_source_name (clock_source));

	engine->control->frame_timer.frames = frame_time_offset;
	engine->control->frame_timer.reset_pending = 0;
	engine->control->frame_timer.current_wakeup = 0;
	engine->control->frame_timer.next_wakeup = 0;
	engine->control->frame_timer.initialized = 0;
	engine->control->frame_timer.filter_coefficient = 0.01;
	engine->control->frame_timer.second_order_integrator = 0;

	engine->first_wakeup = 1;

	engine->control->buffer_size = 0;
	jack_transport_init (engine);
	jack_set_sample_rate (engine, 0);
	engine->control->internal = 0;

	engine->control->has_capabilities = 0;
        
#ifdef JACK_USE_MACH_THREADS
        /* specific resources for server/client real-time thread
	 * communication */
	engine->servertask = mach_task_self();
	if (task_get_bootstrap_port(engine->servertask, &engine->bp)){
		jack_error("Jackd: Can't find bootstrap mach port");
		return NULL;
        }
        engine->portnum = 0;
#endif /* JACK_USE_MACH_THREADS */
        
        
#ifdef USE_CAPABILITIES
	if (uid == 0 || euid == 0) {
		VERBOSE (engine, "running with uid=%d and euid=%d, "
			 "will not try to use capabilites\n",
			 uid, euid);
	} else {
		/* only try to use capabilities if not running as root */
		engine->control->has_capabilities = check_capabilities (engine);
		if (engine->control->has_capabilities == 0) {
			VERBOSE (engine, "required capabilities not "
				 "available\n");
		}
		if (engine->verbose) {
			size_t size;
			cap_t cap = cap_init();
			capgetp(0, cap);
			VERBOSE (engine, "capabilities: %s\n",
				 cap_to_text(cap, &size));
		}
	}
#endif /* USE_CAPABILITIES */

	engine->control->engine_ok = 1;

	snprintf (engine->fifo_prefix, sizeof (engine->fifo_prefix),
		  "%s/jack-ack-fifo-%d",
		  jack_server_dir (engine->server_name, server_dir), getpid ());

	(void) jack_get_fifo_fd (engine, 0);

	jack_client_create_thread (NULL, &engine->server_thread, 0, FALSE,
				   &jack_server_thread, engine);

	return engine;
}

static void
jack_engine_delay (jack_engine_t *engine, float delayed_usecs)
{
	JSList *node;
	jack_event_t event;
	
	engine->control->frame_timer.reset_pending = 1;

	engine->control->xrun_delayed_usecs = delayed_usecs;

	if (delayed_usecs > engine->control->max_delayed_usecs)
		engine->control->max_delayed_usecs = delayed_usecs;

	event.type = XRun;

	jack_lock_graph (engine);
	for (node = engine->clients; node; node = jack_slist_next (node)) {
		jack_deliver_event (engine,
				    (jack_client_internal_t *) node->data,
				    &event);
	}
	jack_unlock_graph (engine);
}

static inline void
jack_inc_frame_time (jack_engine_t *engine, jack_nframes_t nframes)
{
	jack_frame_timer_t *timer = &engine->control->frame_timer;
	jack_time_t now = engine->driver->last_wait_ust; // effective time
	float delta;

	// really need a memory barrier here
	timer->guard1++;

	delta = (int64_t) now - (int64_t) timer->next_wakeup;

	timer->current_wakeup = timer->next_wakeup;
	timer->frames += nframes;
	timer->second_order_integrator += 0.5f * 
		timer->filter_coefficient * delta;	
	timer->next_wakeup = timer->current_wakeup + 
		engine->driver->period_usecs + 
		(int64_t) floorf ((timer->filter_coefficient * 
				   (delta + timer->second_order_integrator)));
	timer->initialized = 1;

	// might need a memory barrier here
	timer->guard2++;
}

static void*
jack_engine_freewheel (void *arg)
{
	jack_engine_t* engine = (jack_engine_t *) arg;

	VERBOSE (engine, "freewheel thread starting ...\n");

	/* we should not be running SCHED_FIFO, so we don't 
	   have to do anything about scheduling.
	*/

	while (engine->freewheeling) {

		jack_lock_graph (engine);

		if (jack_engine_process (engine,
					 engine->control->buffer_size)) {
			jack_error ("process cycle within freewheel failed");
			jack_unlock_graph (engine);
			break;
		}

		jack_unlock_graph (engine);
	}

	VERBOSE (engine, "freewheel came to an end, naturally\n");
	return 0;
}

static int
jack_start_freewheeling (jack_engine_t* engine)
{
	jack_event_t event;

	if (engine->freewheeling) {
		return 0;
	}

	if (engine->driver == NULL) {
		jack_error ("cannot start freewheeling without a driver!");
		return -1;
	}

	/* stop driver before telling anyone about it so 
	   there are no more process() calls being handled.
	*/

	if (engine->driver->stop (engine->driver)) {
		jack_error ("could not stop driver for freewheeling");
		return -1;
	}

	engine->freewheeling = 1;

	event.type = StartFreewheel;
	jack_deliver_event_to_all (engine, &event);
	
	if (jack_client_create_thread (NULL, &engine->freewheel_thread, 0, FALSE,
				       jack_engine_freewheel, engine)) {
		jack_error ("could not start create freewheel thread");
		return -1;
	}

	return 0;
}

static int
jack_stop_freewheeling (jack_engine_t* engine)
{
	jack_event_t event;
	void *ftstatus;

	if (!engine->freewheeling) {
		return 0;
	}

	if (engine->driver == NULL) {
		jack_error ("cannot start freewheeling without a driver!");
		return -1;
	}

	if (!engine->freewheeling) {
		VERBOSE (engine, "stop freewheel when not freewheeling\n");
		return 0;
	}

	/* tell the freewheel thread to stop, and wait for it
	   to exit.
	*/

	engine->freewheeling = 0;
	VERBOSE (engine, "freewheeling stopped, waiting for thread\n");
	pthread_join (engine->freewheel_thread, &ftstatus);
	VERBOSE (engine, "freewheel thread has returned\n");

	/* tell everyone we've stopped */

	event.type = StopFreewheel;
	jack_deliver_event_to_all (engine, &event);

	/* restart the driver */

	if (engine->driver->start (engine->driver)) {
		jack_error ("could not restart driver after freewheeling");
		return -1;
	}
	return 0;
}

static int
jack_run_one_cycle (jack_engine_t *engine, jack_nframes_t nframes,
		    float delayed_usecs)
{
	jack_driver_t* driver = engine->driver;
	int ret = -1;
	static int consecutive_excessive_delays = 0;

#define WORK_SCALE 1.0f

	if (engine->control->real_time &&
	    engine->spare_usecs &&
	    ((WORK_SCALE * engine->spare_usecs) <= delayed_usecs)) {

		MESSAGE("delay of %.3f usecs exceeds estimated spare"
			 " time of %.3f; restart ...\n",
			 delayed_usecs, WORK_SCALE * engine->spare_usecs);
		
		if (++consecutive_excessive_delays > 10) {
			jack_error ("too many consecutive interrupt delays "
				    "... engine pausing");
			return -1;	/* will exit the thread loop */
		}

		jack_engine_delay (engine, delayed_usecs);
		
		return 0;

	} else {
		consecutive_excessive_delays = 0;
	}

	if (jack_try_lock_graph (engine)) {
		/* engine can't run. just throw away an entire cycle */
		driver->null_cycle (driver, nframes);
		return 0;
	}

	if (!engine->freewheeling) {
		DEBUG("waiting for driver read\n");
		if (driver->read (driver, nframes)) {
			goto unlock;
		}
	}
	
	DEBUG("run process\n");

	if (jack_engine_process (engine, nframes) == 0) {
		if (!engine->freewheeling) {
			if (driver->write (driver, nframes)) {
				goto unlock;
			}
		}

	} else {

		JSList *node;
		DEBUG ("engine process cycle failed");

		/* we are already late, or something else went wrong,
		   so it can't hurt to check the existence of all
		   clients.
		*/

		for (node = engine->clients; node;
		     node = jack_slist_next (node)) {
			jack_client_internal_t *client =
				(jack_client_internal_t *) node->data;

			if (client->control->type == ClientExternal) {
				if (kill (client->control->pid, 0)) {
					VERBOSE(engine,
						"client %s has died/exited\n",
						client->control->name);
					client->error++;
				}
			}
			
			DEBUG ("client %s errors = %d", client->control->name,
			       client->error);
		}
	}

	jack_engine_post_process (engine);
	if (delayed_usecs > engine->control->max_delayed_usecs)
		engine->control->max_delayed_usecs = delayed_usecs;
	ret = 0;

  unlock:
	jack_unlock_graph (engine);
	DEBUG("cycle finished, status = %d", ret);
	return ret;
}

static void
jack_engine_driver_exit (jack_engine_t* engine)
{
	/* tell anyone waiting that the driver exited. */
	kill (engine->wait_pid, SIGUSR2);
	engine->driver = NULL;
}

static int
jack_run_cycle (jack_engine_t *engine, jack_nframes_t nframes,
		float delayed_usecs)
{
	jack_nframes_t left;
	jack_nframes_t b_size = engine->control->buffer_size;
	jack_frame_timer_t* timer = &engine->control->frame_timer;
	int no_increment = 0;

	if (engine->first_wakeup) {

		/* the first wakeup */

		timer->next_wakeup = 
			engine->driver->last_wait_ust +
			engine->driver->period_usecs;
		engine->first_wakeup = 0;
		
		/* if we got an xrun/delayed wakeup on the first cycle,
		   reset the pending flag (we have no predicted wakeups
		   to use), but avoid incrementing the frame timer.
		*/

		if (timer->reset_pending) {
			timer->reset_pending = 0;
			no_increment = 1;
		}
	}

	if (timer->reset_pending) {

		/* post xrun-handling */

		/* don't bother to increment the frame counter, because we missed 1 or more 
		   deadlines in the backend anyway.
		 */

		timer->current_wakeup = engine->driver->last_wait_ust;
		timer->next_wakeup = engine->driver->last_wait_ust +
			engine->driver->period_usecs;
		
		timer->reset_pending = 0;

	} else {
		
		/* normal condition */

		if (!no_increment) {
			jack_inc_frame_time (engine, nframes);
		}
	}

	if (engine->verbose) {
		if (nframes != b_size) { 
			VERBOSE (engine, 
				"late driver wakeup: nframes to process = %"
				PRIu32 ".\n", nframes);
		}
	}

	/* run as many cycles as it takes to consume nframes */
	for (left = nframes; left >= b_size; left -= b_size) {
		if (jack_run_one_cycle (engine, b_size, delayed_usecs)) {
			jack_error ("cycle execution failure, exiting");
			return EIO;
		}
	}

	return 0;
}

void 
jack_engine_delete (jack_engine_t *engine)
{
	int i;

	if (engine == NULL)
		return;

	VERBOSE (engine, "starting server engine shutdown\n");

	engine->control->engine_ok = 0;	/* tell clients we're going away */

	/* shutdown master socket to prevent new clients arriving */
	shutdown (engine->fds[0], SHUT_RDWR);
	// close (engine->fds[0]);

	/* now really tell them we're going away */

	for (i = 0; i < engine->pfd_max; ++i) {
		shutdown (engine->pfd[i].fd, SHUT_RDWR);
	}

	if (engine->driver) {
		jack_driver_t* driver = engine->driver;

		VERBOSE (engine, "stopping driver\n");
		driver->stop (driver);
		// VERBOSE (engine, "detaching driver\n");
		// driver->detach (driver, engine);
		VERBOSE (engine, "unloading driver\n");
		jack_driver_unload (driver);
		engine->driver = NULL;
	}

	VERBOSE (engine, "freeing shared port segments\n");
	for (i = 0; i < engine->control->n_port_types; ++i) {
		jack_release_shm (&engine->port_segment[i]);
		jack_destroy_shm (&engine->port_segment[i]);
	}

	/* stop the other engine threads */
	VERBOSE (engine, "stopping server thread\n");

#if JACK_USE_MACH_THREADS 
	// MacOSX pthread_cancel still not implemented correctly in Darwin
	mach_port_t machThread = pthread_mach_thread_np (engine->server_thread);
	thread_terminate (machThread);
#else
	pthread_cancel (engine->server_thread);
	pthread_join (engine->server_thread, NULL);
#endif	

#ifndef JACK_USE_MACH_THREADS 
	/* Cancel the watchdog thread and wait for it to terminate.
	 *
	 * The watchdog thread is not used on MacOSX since CoreAudio
	 * drivers already contain a similar mechanism.
	 */	
	if (engine->control->real_time && engine->watchdog_thread) {
		VERBOSE (engine, "stopping watchdog thread\n");
		pthread_cancel (engine->watchdog_thread);
		pthread_join (engine->watchdog_thread, NULL);
	}
#endif

	VERBOSE (engine, "last xrun delay: %.3f usecs\n",
		engine->control->xrun_delayed_usecs);
	VERBOSE (engine, "max delay reported by backend: %.3f usecs\n",
		engine->control->max_delayed_usecs);

	/* free engine control shm segment */
	engine->control = NULL;
	VERBOSE (engine, "freeing engine shared memory\n");
	jack_release_shm (&engine->control_shm);
	jack_destroy_shm (&engine->control_shm);

	VERBOSE (engine, "max usecs: %.3f, ", engine->max_usecs);

	VERBOSE (engine, "engine deleted\n");
	free (engine);

	jack_messagebuffer_exit();
}

void
jack_port_clear_connections (jack_engine_t *engine,
			     jack_port_internal_t *port)
{
	JSList *node, *next;

	for (node = port->connections; node; ) {
		next = jack_slist_next (node);
		jack_port_disconnect_internal (
			engine, ((jack_connection_internal_t *)
				 node->data)->source,
			((jack_connection_internal_t *)
			 node->data)->destination);
		node = next;
	}

	jack_slist_free (port->connections);
	port->connections = 0;
}

static void
jack_deliver_event_to_all (jack_engine_t *engine, jack_event_t *event)
{
	JSList *node;

	jack_lock_graph (engine);
	for (node = engine->clients; node; node = jack_slist_next (node)) {
		jack_deliver_event (engine,
				    (jack_client_internal_t *) node->data,
				    event);
	}
	jack_unlock_graph (engine);
}

static int
jack_deliver_event (jack_engine_t *engine, jack_client_internal_t *client,
		    jack_event_t *event)
{
	char status;

	/* caller must hold the graph lock */

	DEBUG ("delivering event (type %d)", event->type);

	/* we are not RT-constrained here, so use kill(2) to beef up
	   our check on a client's continued well-being
	*/

	if (client->control->dead
	    || (client->control->type == ClientExternal
		&& kill (client->control->pid, 0))) {
		DEBUG ("client %s is dead - no event sent",
		       client->control->name);
		return 0;
	}

	DEBUG ("client %s is still alive", client->control->name);

	if (jack_client_is_internal (client)) {

		switch (event->type) {
		case PortConnected:
		case PortDisconnected:
			jack_client_handle_port_connection
				(client->control->private_client, event);
			break;

		case BufferSizeChange:
			jack_client_invalidate_port_buffers
				(client->control->private_client);

			if (client->control->bufsize) {
				client->control->bufsize
					(event->x.n,
					 client->control->bufsize_arg);
			}
			break;

		case SampleRateChange:
			if (client->control->srate) {
				client->control->srate
					(event->x.n,
					 client->control->srate_arg);
			}
			break;

		case GraphReordered:
			if (client->control->graph_order) {
				client->control->graph_order
					(client->control->graph_order_arg);
			}
			break;

		case XRun:
			if (client->control->xrun) {
				client->control->xrun
					(client->control->xrun_arg);
			}
			break;

		default:
			/* internal clients don't need to know */
			break;
		}

	} else {

		if (client->control->active) {

			/* there's a thread waiting for events, so
			 * it's worth telling the client */

			DEBUG ("engine writing on event fd");

			if (write (client->event_fd, event, sizeof (*event))
			    != sizeof (*event)) {
				jack_error ("cannot send event to client [%s]"
					    " (%s)", client->control->name,
					    strerror (errno));
				client->error++;
			}
			
			DEBUG ("engine reading from event fd");
			
			if (!client->error &&
			    (read (client->event_fd, &status, sizeof (status))
			     != sizeof (status))) {
				jack_error ("cannot read event response from "
					    "client [%s] (%s)",
					    client->control->name,
					    strerror (errno));
				client->error++;
			}
			
			if (status != 0) {
				jack_error ("bad status for client event "
					    "handling (type = %d)",
					    event->type);
				client->error++;
			}
		}
	}

	DEBUG ("event delivered");

	return 0;
}

int
jack_rechain_graph (jack_engine_t *engine)
{
	JSList *node, *next;
	unsigned long n;
	int err = 0;
	jack_client_internal_t *client, *subgraph_client, *next_client;
	jack_event_t event;
	int upstream_is_jackd;

	jack_clear_fifos (engine);

	subgraph_client = 0;

	VERBOSE(engine, "++ jack_rechain_graph():\n");

	event.type = GraphReordered;

	for (n = 0, node = engine->clients, next = NULL; node; node = next) {

		next = jack_slist_next (node);

		if (((jack_client_internal_t *) node->data)->control->active) {

			client = (jack_client_internal_t *) node->data;

			/* find the next active client. its ok for
			 * this to be NULL */
			
			while (next) {
				if (((jack_client_internal_t *)
				     next->data)->control->active) {
					break;
				}
				next = jack_slist_next (next);
			};

			if (next == NULL) {
				next_client = NULL;
			} else {
				next_client = (jack_client_internal_t *)
					next->data;
			}

			client->execution_order = n;
			client->next_client = next_client;
			
			if (jack_client_is_internal (client)) {
				
				/* break the chain for the current
				 * subgraph. the server will wait for
				 * chain on the nth FIFO, and will
				 * then execute this internal
				 * client. */
				
				if (subgraph_client) {
					subgraph_client->subgraph_wait_fd =
						jack_get_fifo_fd (engine, n);
					VERBOSE (engine, "client %s: wait_fd="
						 "%d, execution_order="
						 "%lu.\n", 
						 subgraph_client->
						 control->name,
						 subgraph_client->
						 subgraph_wait_fd, n);
					n++;
				}

				VERBOSE (engine, "client %s: internal "
					 "client, execution_order="
					 "%lu.\n", 
					 client->control->name, n);

				/* this does the right thing for
				 * internal clients too 
				 */

				jack_deliver_event (engine, client, &event);

				subgraph_client = 0;

			} else {
				
				if (subgraph_client == NULL) {
					
				        /* start a new subgraph. the
					 * engine will start the chain
					 * by writing to the nth
					 * FIFO. 
					 */
					
					subgraph_client = client;
					subgraph_client->subgraph_start_fd =
						jack_get_fifo_fd (engine, n);
					VERBOSE (engine, "client %s: "
						 "start_fd=%d, execution"
						 "_order=%lu.\n",
						 subgraph_client->
						 control->name,
						 subgraph_client->
						 subgraph_start_fd, n);
					
					/* this external client after
					   this will have jackd as its
					   upstream connection.
					*/
					
					upstream_is_jackd = 1;

				} 
				else {
					VERBOSE (engine, "client %s: in"
						 " subgraph after %s, "
						 "execution_order="
						 "%lu.\n",
						 client->control->name,
						 subgraph_client->
						 control->name, n);
					subgraph_client->subgraph_wait_fd = -1;
					
					/* this external client after
					   this will have another
					   client as its upstream
					   connection.
					*/
					
					upstream_is_jackd = 0;
				}

				/* make sure fifo for 'n + 1' exists
				 * before issuing client reorder
				 */
				(void) jack_get_fifo_fd(
					engine, client->execution_order + 1);
				event.x.n = client->execution_order;
				event.y.n = upstream_is_jackd;
				jack_deliver_event (engine, client, &event);
				n++;
			}
		}
	}

	if (subgraph_client) {
		subgraph_client->subgraph_wait_fd =
			jack_get_fifo_fd (engine, n);
		VERBOSE (engine, "client %s: wait_fd=%d, "
			 "execution_order=%lu (last client).\n", 
			 subgraph_client->control->name,
			 subgraph_client->subgraph_wait_fd, n);
	}

	VERBOSE (engine, "-- jack_rechain_graph()\n");

	return err;
}

static jack_nframes_t
jack_get_port_total_latency (jack_engine_t *engine,
			     jack_port_internal_t *port, int hop_count,
			     int toward_port)
{
	JSList *node;
	jack_nframes_t latency;
	jack_nframes_t max_latency = 0;

#ifdef DEBUG_TOTAL_LATENCY_COMPUTATION
	char prefix[32];
	int i;

	for (i = 0; i < hop_count; ++i) {
		prefix[i] = '\t';
	}

	prefix[i] = '\0';
#endif

	/* call tree must hold engine->client_lock. */

	latency = port->shared->latency;

	/* we don't prevent cyclic graphs, so we have to do something
	   to bottom out in the event that they are created.
	*/

	if (hop_count > 8) {
		return latency;
	}

#ifdef DEBUG_TOTAL_LATENCY_COMPUTATION
	fprintf (stderr, "\n%sFor port %s (%s)\n", prefix, port->shared->name, (toward_port ? "toward" : "away"));
#endif
	
	for (node = port->connections; node; node = jack_slist_next (node)) {

		jack_nframes_t this_latency;
		jack_connection_internal_t *connection;

		connection = (jack_connection_internal_t *) node->data;

		
		if ((toward_port &&
		     (connection->source->shared == port->shared)) ||
		    (!toward_port &&
		     (connection->destination->shared == port->shared))) {

#ifdef DEBUG_TOTAL_LATENCY_COMPUTATION
			fprintf (stderr, "%s\tskip connection %s->%s\n",
				 prefix,
				 connection->source->shared->name,
				 connection->destination->shared->name);
#endif

			continue;
		}

#ifdef DEBUG_TOTAL_LATENCY_COMPUTATION
		fprintf (stderr, "%s\tconnection %s->%s ... ", 
			 prefix,
			 connection->source->shared->name,
			 connection->destination->shared->name);
#endif
		/* if we're a destination in the connection, recurse
		   on the source to get its total latency
		*/

		if (connection->destination == port) {

			if (connection->source->shared->flags
			    & JackPortIsTerminal) {
				this_latency = connection->source->
					shared->latency;
			} else {
				this_latency =
					jack_get_port_total_latency (
						engine, connection->source,
						hop_count + 1, 
						toward_port);
			}

		} else {

			/* "port" is the source, so get the latency of
			 * the destination */
			if (connection->destination->shared->flags
			    & JackPortIsTerminal) {
				this_latency = connection->destination->
					shared->latency;
			} else {
				this_latency =
					jack_get_port_total_latency (
						engine,
						connection->destination,
						hop_count + 1, 
						toward_port);
			}
		}

		if (this_latency > max_latency) {
			max_latency = this_latency;
		}
	}

#ifdef DEBUG_TOTAL_LATENCY_COMPUTATION
	fprintf (stderr, "%s\treturn %lu + %lu = %lu\n", prefix, latency, max_latency, latency + max_latency);
#endif	

	return latency + max_latency;
}

static void
jack_compute_all_port_total_latencies (jack_engine_t *engine)
{
	jack_port_shared_t *shared = engine->control->ports;
	unsigned int i;
	int toward_port;

	for (i = 0; i < engine->control->port_max; i++) {
		if (shared[i].in_use) {
			if (shared[i].flags & JackPortIsOutput) {
				toward_port = FALSE;
			} else {
				toward_port = TRUE;
			}
			shared[i].total_latency =
				jack_get_port_total_latency (
					engine, &engine->internal_ports[i],
					0, toward_port);
		}
	}
}

/* How the sort works:
 *
 * Each client has a "sortfeeds" list of clients indicating which clients
 * it should be considered as feeding for the purposes of sorting the
 * graph. This list differs from the clients it /actually/ feeds in the
 * following ways:
 *
 * 1. Connections from a client to itself are disregarded
 *
 * 2. Connections to a driver client are disregarded
 *
 * 3. If a connection from A to B is a feedback connection (ie there was
 *    already a path from B to A when the connection was made) then instead
 *    of B appearing on A's sortfeeds list, A will appear on B's sortfeeds
 *    list.
 *
 * If client A is on client B's sortfeeds list, client A must come after
 * client B in the execution order. The above 3 rules ensure that the
 * sortfeeds relation is always acyclic so that all ordering constraints
 * can actually be met. 
 *
 * Each client also has a "truefeeds" list which is the same as sortfeeds
 * except that feedback connections appear normally instead of reversed.
 * This is used to detect whether the graph has become acyclic.
 *
 */ 
 
void
jack_sort_graph (jack_engine_t *engine)
{
	/* called, obviously, must hold engine->client_lock */

	engine->clients = jack_slist_sort (engine->clients,
					   (JCompareFunc) jack_client_sort);
	jack_compute_all_port_total_latencies (engine);
	jack_rechain_graph (engine);
}

static int 
jack_client_sort (jack_client_internal_t *a, jack_client_internal_t *b)
{
	/* drivers are forced to the front, ie considered as sources
	   rather than sinks for purposes of the sort */

	if (jack_client_feeds_transitive (a, b) ||
	    (a->control->type == ClientDriver &&
	     b->control->type != ClientDriver)) {
		return -1;
	} else if (jack_client_feeds_transitive (b, a) ||
		   (b->control->type == ClientDriver &&
		    a->control->type != ClientDriver)) {
		return 1;
	} else {
		return 0;
	}
}

/* transitive closure of the relation expressed by the sortfeeds lists. */
static int
jack_client_feeds_transitive (jack_client_internal_t *source,
			      jack_client_internal_t *dest )
{
	jack_client_internal_t *med;
	JSList *node;
	
	if (jack_slist_find (source->sortfeeds, dest)) {
		return 1;
	}

	for (node = source->sortfeeds; node; node = jack_slist_next (node)) {

		med = (jack_client_internal_t *) node->data;

		if (jack_client_feeds_transitive (med, dest)) {
			return 1;
		}
	}

	return 0;
}

/**
 * Checks whether the graph has become acyclic and if so modifies client
 * sortfeeds lists to turn leftover feedback connections into normal ones.
 * This lowers latency, but at the expense of some data corruption.
 */
static void
jack_check_acyclic (jack_engine_t *engine)
{
	JSList *srcnode, *dstnode, *portnode, *connnode;
	jack_client_internal_t *src, *dst;
	jack_port_internal_t *port;
	jack_connection_internal_t *conn;
	int stuck;
	int unsortedclients = 0;

	VERBOSE (engine, "checking for graph become acyclic\n");

	for (srcnode = engine->clients; srcnode;
	     srcnode = jack_slist_next (srcnode)) {

		src = (jack_client_internal_t *) srcnode->data;
		src->tfedcount = src->fedcount;
		unsortedclients++;
	}
	
	stuck = FALSE;

	/* find out whether a normal sort would have been possible */
	while (unsortedclients && !stuck) {
	
		stuck = TRUE;

		for (srcnode = engine->clients; srcnode;
	     	     srcnode = jack_slist_next (srcnode)) {

			src = (jack_client_internal_t *) srcnode->data;
			
			if (!src->tfedcount) {
			
				stuck = FALSE;
				unsortedclients--;
				src->tfedcount = -1;
				
				for (dstnode = src->truefeeds; dstnode;
				     dstnode = jack_slist_next (dstnode)) {
				     
					dst = (jack_client_internal_t *)
						dstnode->data;
					dst->tfedcount--;
				}
			}
		}
	}
	
	if (stuck) {

		VERBOSE (engine, "graph is still cyclic\n" );
	} else {

		VERBOSE (engine, "graph has become acyclic\n");

		/* turn feedback connections around in sortfeeds */
		for (srcnode = engine->clients; srcnode;
		     srcnode = jack_slist_next (srcnode)) {

			src = (jack_client_internal_t *) srcnode->data;

			for (portnode = src->ports; portnode;
			     portnode = jack_slist_next (portnode)) {

				port = (jack_port_internal_t *) portnode->data;
			
				for (connnode = port->connections; connnode;
				     connnode = jack_slist_next (connnode)) {
				
					conn = (jack_connection_internal_t*)
						connnode->data;
				
					if (conn->dir == -1 )
					
					/*&& 
						conn->srcclient == src) */{
				
						VERBOSE (engine,
						"reversing connection from "
						"%s to %s\n",
						conn->srcclient->control->name,
						conn->dstclient->control->name);
						conn->dir = 1;
						conn->dstclient->sortfeeds = 
						  jack_slist_remove
						    (conn->dstclient->sortfeeds,
						     conn->srcclient);
					     
						conn->srcclient->sortfeeds =
						  jack_slist_prepend
						    (conn->srcclient->sortfeeds,
						     conn->dstclient );
					}
				}
			}
		}
		engine->feedbackcount = 0;
	}
}

/**
 * Dumps current engine configuration to stderr.
 */
void jack_dump_configuration(jack_engine_t *engine, int take_lock)
{
        JSList *clientnode, *portnode, *connectionnode;
	jack_client_internal_t *client;
	jack_client_control_t *ctl;
	jack_port_internal_t *port;
	jack_connection_internal_t* connection;
	int n, m, o;
	
	fprintf(stderr, "engine.c: <-- dump begins -->\n");

	if (take_lock) {
		jack_lock_graph (engine);
	}

	for (n = 0, clientnode = engine->clients; clientnode;
	     clientnode = jack_slist_next (clientnode)) {
	        client = (jack_client_internal_t *) clientnode->data;
		ctl = client->control;

		fprintf (stderr, "client #%d: %s (type: %d, process? %s,"
			 " start=%d wait=%d\n",
			 ++n,
			 ctl->name,
			 ctl->type,
			 ctl->process ? "yes" : "no",
			 client->subgraph_start_fd,
			 client->subgraph_wait_fd);

		for(m = 0, portnode = client->ports; portnode;
		    portnode = jack_slist_next (portnode)) {
		        port = (jack_port_internal_t *) portnode->data;

			fprintf(stderr, "\t port #%d: %s\n", ++m,
				port->shared->name);

			for(o = 0, connectionnode = port->connections; 
			    connectionnode; 
			    connectionnode =
				    jack_slist_next (connectionnode)) {
			        connection = (jack_connection_internal_t *)
					connectionnode->data;
	
				fprintf(stderr, "\t\t connection #%d: %s %s\n",
					++o,
					(port->shared->flags
					 & JackPortIsInput)? "<-": "->",
					(port->shared->flags & JackPortIsInput)?
					connection->source->shared->name:
					connection->destination->shared->name);
			}
		}
	}

	if (take_lock) {
		jack_unlock_graph (engine);
	}

	
	fprintf(stderr, "engine.c: <-- dump ends -->\n");
}

static int 
jack_port_do_connect (jack_engine_t *engine,
		       const char *source_port,
		       const char *destination_port)
{
	jack_connection_internal_t *connection;
	jack_port_internal_t *srcport, *dstport;
	jack_port_id_t src_id, dst_id;
	jack_client_internal_t *srcclient, *dstclient;
	JSList *it;

	if ((srcport = jack_get_port_by_name (engine, source_port)) == NULL) {
		jack_error ("unknown source port in attempted connection [%s]",
			    source_port);
		return -1;
	}

	if ((dstport = jack_get_port_by_name (engine, destination_port))
	    == NULL) {
		jack_error ("unknown destination port in attempted connection"
			    " [%s]", destination_port);
		return -1;
	}

	if ((dstport->shared->flags & JackPortIsInput) == 0) {
		jack_error ("destination port in attempted connection of"
			    " %s and %s is not an input port", 
			    source_port, destination_port);
		return -1;
	}

	if ((srcport->shared->flags & JackPortIsOutput) == 0) {
		jack_error ("source port in attempted connection of %s and"
			    " %s is not an output port",
			    source_port, destination_port);
		return -1;
	}

	if (srcport->shared->ptype_id != dstport->shared->ptype_id) {
		jack_error ("ports used in attemped connection are not of "
			    "the same data type");
		return -1;
	}

	if ((srcclient = jack_client_internal_by_id (engine,
						  srcport->shared->client_id))
	    == 0) {
		jack_error ("unknown client set as owner of port - "
			    "cannot connect");
		return -1;
	}
	
	if (!srcclient->control->active) {
		jack_error ("cannot connect ports owned by inactive clients;"
			    " \"%s\" is not active", srcclient->control->name);
		return -1;
	}

	if ((dstclient = jack_client_internal_by_id (engine,
						  dstport->shared->client_id))
	    == 0) {
		jack_error ("unknown client set as owner of port - cannot "
			    "connect");
		return -1;
	}
	
	if (!dstclient->control->active) {
		jack_error ("cannot connect ports owned by inactive clients;"
			    " \"%s\" is not active", dstclient->control->name);
		return -1;
	}

	for (it = srcport->connections; it; it = it->next) {
		if (((jack_connection_internal_t *)it->data)->destination
		    == dstport) {
			return EEXIST;
		}
	}

	connection = (jack_connection_internal_t *)
		malloc (sizeof (jack_connection_internal_t));

	connection->source = srcport;
	connection->destination = dstport;
	connection->srcclient = srcclient;
	connection->dstclient = dstclient;

	src_id = srcport->shared->id;
	dst_id = dstport->shared->id;

	jack_lock_graph (engine);

	if (dstport->connections && !dstport->shared->has_mixdown) {
		jack_port_type_info_t *port_type =
			jack_port_type_info (engine, dstport);
		jack_error ("cannot make multiple connections to a port of"
			    " type [%s]", port_type->type_name);
		free (connection);
		jack_unlock_graph (engine);
		return -1;
	} else {

		if (dstclient->control->type == ClientDriver)
		{
			/* Ignore output connections to drivers for purposes
			   of sorting. Drivers are executed first in the sort
			   order anyway, and we don't want to treat graphs
			   such as driver -> client -> driver as containing
			   feedback */
			
			VERBOSE (engine,
				 "connect %s and %s (output)\n",
				 srcport->shared->name,
				 dstport->shared->name);

			connection->dir = 1;

		}
		else if (srcclient != dstclient) {
		
			srcclient->truefeeds = jack_slist_prepend
				(srcclient->truefeeds, dstclient);

			dstclient->fedcount++;				

			if (jack_client_feeds_transitive (dstclient,
							  srcclient ) ||
			    (dstclient->control->type == ClientDriver &&
			     srcclient->control->type != ClientDriver)) {
		    
				/* dest is running before source so
				   this is a feedback connection */
				
				VERBOSE (engine,
					 "connect %s and %s (feedback)\n",
					 srcport->shared->name,
					 dstport->shared->name);
				 
				dstclient->sortfeeds = jack_slist_prepend
					(dstclient->sortfeeds, srcclient);

				connection->dir = -1;
				engine->feedbackcount++;
				VERBOSE (engine,
					 "feedback count up to %d\n",
					 engine->feedbackcount);

			} else {
		
				/* this is not a feedback connection */

				VERBOSE (engine,
					 "connect %s and %s (forward)\n",
					 srcport->shared->name,
					 dstport->shared->name);

				srcclient->sortfeeds = jack_slist_prepend
					(srcclient->sortfeeds, dstclient);

				connection->dir = 1;
			}
		}
		else
		{
			/* this is a connection to self */

			VERBOSE (engine,
				 "connect %s and %s (self)\n",
				 srcport->shared->name,
				 dstport->shared->name);
			
			connection->dir = 0;
		}

		dstport->connections =
			jack_slist_prepend (dstport->connections, connection);
		srcport->connections =
			jack_slist_prepend (srcport->connections, connection);
		
		DEBUG ("actually sorted the graph...");

		jack_send_connection_notification (engine,
						   srcport->shared->client_id,
						   src_id, dst_id, TRUE);
		jack_send_connection_notification (engine,
						   dstport->shared->client_id,
						   dst_id, src_id, TRUE);
						   
		jack_sort_graph (engine);
	}

	jack_unlock_graph (engine);
	return 0;
}

int
jack_port_disconnect_internal (jack_engine_t *engine, 
			       jack_port_internal_t *srcport, 
			       jack_port_internal_t *dstport )

{
	JSList *node;
	jack_connection_internal_t *connect;
	int ret = -1;
	jack_port_id_t src_id, dst_id;
	int check_acyclic = engine->feedbackcount;

	/* call tree **** MUST HOLD **** engine->client_lock. */
	for (node = srcport->connections; node;
	     node = jack_slist_next (node)) {

		connect = (jack_connection_internal_t *) node->data;

		if (connect->source == srcport &&
		    connect->destination == dstport) {

			VERBOSE (engine, "DIS-connect %s and %s\n",
				 srcport->shared->name,
				 dstport->shared->name);
			
			srcport->connections =
				jack_slist_remove (srcport->connections,
						   connect);
			dstport->connections =
				jack_slist_remove (dstport->connections,
						   connect);

			src_id = srcport->shared->id;
			dst_id = dstport->shared->id;

			/* this is a bit harsh, but it basically says
			   that if we actually do a disconnect, and
			   its the last one, then make sure that any
			   input monitoring is turned off on the
			   srcport. this isn't ideal for all
			   situations, but it works better for most of
			   them.
			*/
			if (srcport->connections == NULL) {
				srcport->shared->monitor_requests = 0;
			}

			jack_send_connection_notification (
				engine, srcport->shared->client_id, src_id,
				dst_id, FALSE);
			jack_send_connection_notification (
				engine, dstport->shared->client_id, dst_id,
				src_id, FALSE);

			if (connect->dir) {
			
				jack_client_internal_t *src;
				jack_client_internal_t *dst;
			
				src = jack_client_internal_by_id 
					(engine, srcport->shared->client_id);

				dst =  jack_client_internal_by_id
					(engine, dstport->shared->client_id);
								    
				src->truefeeds = jack_slist_remove
					(src->truefeeds, dst);

				dst->fedcount--;					
				
				if (connect->dir == 1) {
					/* normal connection: remove dest from
					   source's sortfeeds list */ 
					src->sortfeeds = jack_slist_remove
						(src->sortfeeds, dst);
				} else {
					/* feedback connection: remove source
					   from dest's sortfeeds list */
					dst->sortfeeds = jack_slist_remove
						(dst->sortfeeds, src);
					engine->feedbackcount--;
					VERBOSE (engine,
						 "feedback count down to %d\n",
						 engine->feedbackcount);
					
				}
			} /* else self-connection: do nothing */

			free (connect);
			ret = 0;
			break;
		}
	}

	if (check_acyclic) {
		jack_check_acyclic (engine);
	}
	
	jack_sort_graph (engine);

	return ret;
}

static int
jack_port_do_disconnect_all (jack_engine_t *engine,
			     jack_port_id_t port_id)
{
	if (port_id >= engine->control->port_max) {
		jack_error ("illegal port ID in attempted disconnection [%"
			    PRIu32 "]", port_id);
		return -1;
	}

	VERBOSE (engine, "clear connections for %s\n",
		 engine->internal_ports[port_id].shared->name);

	jack_lock_graph (engine);
	jack_port_clear_connections (engine, &engine->internal_ports[port_id]);
	jack_sort_graph (engine);
	jack_unlock_graph (engine);

	return 0;
}

static int 
jack_port_do_disconnect (jack_engine_t *engine,
			 const char *source_port,
			 const char *destination_port)
{
	jack_port_internal_t *srcport, *dstport;
	int ret = -1;

	if ((srcport = jack_get_port_by_name (engine, source_port)) == NULL) {
		jack_error ("unknown source port in attempted disconnection"
			    " [%s]", source_port);
		return -1;
	}

	if ((dstport = jack_get_port_by_name (engine, destination_port))
	    == NULL) {
		jack_error ("unknown destination port in attempted"
			    " disconnection [%s]", destination_port);
		return -1;
	}

	jack_lock_graph (engine);

	ret = jack_port_disconnect_internal (engine, srcport, dstport);

	jack_unlock_graph (engine);

	return ret;
}

int 
jack_get_fifo_fd (jack_engine_t *engine, unsigned int which_fifo)
{
	/* caller must hold client_lock */
	char path[PATH_MAX+1];
	struct stat statbuf;

	snprintf (path, sizeof (path), "%s-%d", engine->fifo_prefix,
		  which_fifo);

	DEBUG ("%s", path);

	if (stat (path, &statbuf)) {
		if (errno == ENOENT) {

			if (mkfifo(path, 0666) < 0){
				jack_error ("cannot create inter-client FIFO"
					    " [%s] (%s)\n", path,
					    strerror (errno));
				return -1;
			}

		} else {
			jack_error ("cannot check on FIFO %d\n", which_fifo);
			return -1;
		}
	} else {
		if (!S_ISFIFO(statbuf.st_mode)) {
			jack_error ("FIFO %d (%s) already exists, but is not"
				    " a FIFO!\n", which_fifo, path);
			return -1;
		}
	}

	if (which_fifo >= engine->fifo_size) {
		unsigned int i;

		engine->fifo = (int *)
			realloc (engine->fifo,
				 sizeof (int) * (engine->fifo_size + 16));
		for (i = engine->fifo_size; i < engine->fifo_size + 16; i++) {
			engine->fifo[i] = -1;
		}
		engine->fifo_size += 16;
	}

	if (engine->fifo[which_fifo] < 0) {
		if ((engine->fifo[which_fifo] =
		     open (path, O_RDWR|O_CREAT|O_NONBLOCK, 0666)) < 0) {
			jack_error ("cannot open fifo [%s] (%s)", path,
				    strerror (errno));
			return -1;
		}
		DEBUG ("opened engine->fifo[%d] == %d (%s)",
		       which_fifo, engine->fifo[which_fifo], path);
	}

	return engine->fifo[which_fifo];
}

static void
jack_clear_fifos (jack_engine_t *engine)
{
	/* caller must hold client_lock */

	unsigned int i;
	char buf[16];

	/* this just drains the existing FIFO's of any data left in
	   them by aborted clients, etc. there is only ever going to
	   be 0, 1 or 2 bytes in them, but we'll allow for up to 16.
	*/
	for (i = 0; i < engine->fifo_size; i++) {
		if (engine->fifo[i] >= 0) {
			int nread = read (engine->fifo[i], buf, sizeof (buf));

			if (nread < 0 && errno != EAGAIN) {
				jack_error ("clear fifo[%d] error: %s",
					    i, strerror (errno));
			} 
		}
	}
}

static int
jack_use_driver (jack_engine_t *engine, jack_driver_t *driver)
{
	if (engine->driver) {
		engine->driver->detach (engine->driver, engine);
		engine->driver = 0;
	}

	if (driver) {
		if (driver->attach (driver, engine))
			return -1;

		engine->rolling_interval =
			jack_rolling_interval (driver->period_usecs);
	}

	engine->driver = driver;
	return 0;
}


/* PORT RELATED FUNCTIONS */


static jack_port_id_t
jack_get_free_port (jack_engine_t *engine)

{
	jack_port_id_t i;

	pthread_mutex_lock (&engine->port_lock);

	for (i = 0; i < engine->port_max; i++) {
		if (engine->control->ports[i].in_use == 0) {
			engine->control->ports[i].in_use = 1;
			break;
		}
	}
	
	pthread_mutex_unlock (&engine->port_lock);
	
	if (i == engine->port_max) {
		return (jack_port_id_t) -1;
	}

	return i;
}

void
jack_port_release (jack_engine_t *engine, jack_port_internal_t *port)
{
	pthread_mutex_lock (&engine->port_lock);
	port->shared->in_use = 0;

	if (port->buffer_info) {
		jack_port_buffer_list_t *blist =
			jack_port_buffer_list (engine, port);
		pthread_mutex_lock (&blist->lock);
		blist->freelist =
			jack_slist_prepend (blist->freelist,
					    port->buffer_info);
		port->buffer_info = NULL;
		pthread_mutex_unlock (&blist->lock);
	}
	pthread_mutex_unlock (&engine->port_lock);
}

jack_port_internal_t *
jack_get_port_internal_by_name (jack_engine_t *engine, const char *name)
{
	jack_port_id_t id;

	pthread_mutex_lock (&engine->port_lock);

	for (id = 0; id < engine->port_max; id++) {
		if (strcmp (engine->control->ports[id].name, name) == 0) {
			break;
		}
	}

	pthread_mutex_unlock (&engine->port_lock);
	
	if (id != engine->port_max) {
		return &engine->internal_ports[id];
	} else {
		return NULL;
	}
}

int
jack_port_do_register (jack_engine_t *engine, jack_request_t *req)
	
{
	jack_port_id_t port_id;
	jack_port_shared_t *shared;
	jack_port_internal_t *port;
	jack_client_internal_t *client;
	unsigned long i;

	for (i = 0; i < engine->control->n_port_types; ++i) {
		if (strcmp (req->x.port_info.type,
			    engine->control->port_types[i].type_name) == 0) {
			break;
		}
	}

	if (i == engine->control->n_port_types) {
		jack_error ("cannot register a port of type \"%s\"",
			    req->x.port_info.type);
		return -1;
	}

	jack_lock_graph (engine);
	if ((client = jack_client_internal_by_id (engine,
						  req->x.port_info.client_id))
	    == NULL) {
		jack_error ("unknown client id in port registration request");
		return -1;
	}
	jack_unlock_graph (engine);

	if ((port_id = jack_get_free_port (engine)) == (jack_port_id_t) -1) {
		jack_error ("no ports available!");
		return -1;
	}

	shared = &engine->control->ports[port_id];

	strcpy (shared->name, req->x.port_info.name);
	shared->ptype_id = engine->control->port_types[i].ptype_id;
	shared->client_id = req->x.port_info.client_id;
	shared->flags = req->x.port_info.flags;
	shared->latency = 0;
	shared->monitor_requests = 0;

	port = &engine->internal_ports[port_id];

	port->shared = shared;
	port->connections = 0;
	port->buffer_info = NULL;
	
	if (jack_port_assign_buffer (engine, port)) {
		jack_error ("cannot assign buffer for port");
		return -1;
	}

	jack_lock_graph (engine);
	client->ports = jack_slist_prepend (client->ports, port);
	jack_port_registration_notify (engine, port_id, TRUE);
	jack_unlock_graph (engine);

	VERBOSE (engine, "registered port %s, offset = %u\n",
		 shared->name, (unsigned int)shared->offset);

	req->x.port_info.port_id = port_id;

	return 0;
}

int
jack_port_do_unregister (jack_engine_t *engine, jack_request_t *req)
{
	jack_client_internal_t *client;
	jack_port_shared_t *shared;
	jack_port_internal_t *port;

	if (req->x.port_info.port_id < 0 ||
	    req->x.port_info.port_id > engine->port_max) {
		jack_error ("invalid port ID %" PRIu32
			    " in unregister request",
			    req->x.port_info.port_id);
		return -1;
	}

	shared = &engine->control->ports[req->x.port_info.port_id];

	if (shared->client_id != req->x.port_info.client_id) {
		jack_error ("Client %" PRIu32
			    " is not allowed to remove port %s",
			    req->x.port_info.client_id, shared->name);
		return -1;
	}

	jack_lock_graph (engine);
	if ((client = jack_client_internal_by_id (engine, shared->client_id))
	    == NULL) {
		jack_error ("unknown client id in port registration request");
		jack_unlock_graph (engine);
		return -1;
	}

	port = &engine->internal_ports[req->x.port_info.port_id];

	jack_port_clear_connections (engine, port);
	jack_port_release (engine,
			   &engine->internal_ports[req->x.port_info.port_id]);
	
	client->ports = jack_slist_remove (client->ports, port);
	jack_port_registration_notify (engine, req->x.port_info.port_id,
				       FALSE);
	jack_unlock_graph (engine);

	return 0;
}

int
jack_do_get_port_connections (jack_engine_t *engine, jack_request_t *req,
			      int reply_fd)
{
	jack_port_internal_t *port;
	JSList *node;
	unsigned int i;
	int ret = -1;
	int internal = FALSE;

	jack_lock_graph (engine);

	port = &engine->internal_ports[req->x.port_info.port_id];

	DEBUG ("Getting connections for port '%s'.", port->shared->name);

	req->x.port_connections.nports = jack_slist_length (port->connections);
	req->status = 0;

	/* figure out if this is an internal or external client */

	for (node = engine->clients; node; node = jack_slist_next (node)) {
		
		if (((jack_client_internal_t *) node->data)->request_fd
		    == reply_fd) {
			internal = jack_client_is_internal(
				(jack_client_internal_t *) node->data);
			break;
		}
	}

	if (!internal) {
		if (write (reply_fd, req, sizeof (*req))
		    < (ssize_t) sizeof (req)) {
			jack_error ("cannot write GetPortConnections result "
				    "to client via fd = %d (%s)", 
				    reply_fd, strerror (errno));
			goto out;
		}
	} else {
		req->x.port_connections.ports = (const char **)
			malloc (sizeof (char *)
				* req->x.port_connections.nports);
	}

	if (req->type == GetPortConnections) {
		
		for (i = 0, node = port->connections; node;
		     node = jack_slist_next (node), ++i) {

			jack_port_id_t port_id;
			
			if (((jack_connection_internal_t *) node->data)->source
			    == port) {
				port_id = ((jack_connection_internal_t *)
					   node->data)->destination->shared->id;
			} else {
				port_id = ((jack_connection_internal_t *)
					   node->data)->source->shared->id;
			}
			
			if (internal) {

				/* internal client asking for
				 * names. store in malloc'ed space,
				 * client frees
				 */
				req->x.port_connections.ports[i] =
					engine->control->ports[port_id].name;

			} else {

				/* external client asking for
				 * names. we write the port id's to
				 * the reply fd.
				 */
				if (write (reply_fd, &port_id,
					   sizeof (port_id))
				    < (ssize_t) sizeof (port_id)) {
					jack_error ("cannot write port id "
						    "to client");
					goto out;
				}
			}
		}
	}

	ret = 0;

  out:
	req->status = ret;
	jack_unlock_graph (engine);
	return ret;
}

void
jack_port_registration_notify (jack_engine_t *engine,
			       jack_port_id_t port_id, int yn)
{
	jack_event_t event;
	jack_client_internal_t *client;
	JSList *node;

	event.type = (yn ? PortRegistered : PortUnregistered);
	event.x.port_id = port_id;
	
	for (node = engine->clients; node; node = jack_slist_next (node)) {
		
		client = (jack_client_internal_t *) node->data;

		if (!client->control->active) {
			continue;
		}

		if (client->control->port_register) {
			if (jack_deliver_event (engine, client, &event)) {
				jack_error ("cannot send port registration"
					    " notification to %s (%s)",
					     client->control->name,
					    strerror (errno));
			}
		}
	}
}

int
jack_port_assign_buffer (jack_engine_t *engine, jack_port_internal_t *port)
{
	jack_port_buffer_list_t *blist =
		jack_port_buffer_list (engine, port);
	jack_port_buffer_info_t *bi;

	if (port->shared->flags & JackPortIsInput) {
		port->shared->offset = 0;
		return 0;
	}
	
	pthread_mutex_lock (&blist->lock);

	if (blist->freelist == NULL) {
		jack_port_type_info_t *port_type =
			jack_port_type_info (engine, port);
		jack_error ("all %s port buffers in use!",
			    port_type->type_name);
		pthread_mutex_unlock (&blist->lock);
		return -1;
	}

	bi = (jack_port_buffer_info_t *) blist->freelist->data;
	blist->freelist = jack_slist_remove (blist->freelist, bi);

	port->shared->offset = bi->offset;
	port->buffer_info = bi;

	pthread_mutex_unlock (&blist->lock);
	return 0;
}

static jack_port_internal_t *
jack_get_port_by_name (jack_engine_t *engine, const char *name)
{
	jack_port_id_t id;

	/* Note the potential race on "in_use". Other design
	   elements prevent this from being a problem.
	*/

	for (id = 0; id < engine->port_max; id++) {
		if (engine->control->ports[id].in_use &&
		    strcmp (engine->control->ports[id].name, name) == 0) {
			return &engine->internal_ports[id];
		}
	}

	return NULL;
}

static int
jack_send_connection_notification (jack_engine_t *engine,
				   jack_client_id_t client_id, 
				   jack_port_id_t self_id,
				   jack_port_id_t other_id, int connected)

{
	jack_client_internal_t *client;
	jack_event_t event;

	if ((client = jack_client_internal_by_id (engine, client_id)) == NULL) {
		jack_error ("no such client %" PRIu32
			    " during connection notification", client_id);
		return -1;
	}

	if (client->control->active) {
		event.type = (connected ? PortConnected : PortDisconnected);
		event.x.self_id = self_id;
		event.y.other_id = other_id;
		
		if (jack_deliver_event (engine, client, &event)) {
			jack_error ("cannot send port connection notification"
				    " to client %s (%s)", 
				    client->control->name, strerror (errno));
			return -1;
		}
	}

	return 0;
}
