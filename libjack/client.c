/*
    Copyright (C) 2001-2003 Paul Davis
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public License
    along with this program; if not, write to the Free Software 
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

    $Id$
*/

#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <regex.h>
#include <math.h>

#include <config.h>

#include <jack/jack.h>
#include <jack/internal.h>
#include <jack/engine.h>
#include <jack/pool.h>
#include <jack/error.h>
#include <jack/time.h>
#include <jack/jslist.h>
#include <jack/version.h>

#include "local.h"

#ifdef WITH_TIMESTAMPS
#include <jack/timestamps.h>
#endif /* WITH_TIMESTAMPS */

jack_time_t __jack_cpu_mhz;

char *jack_server_dir = "/tmp";

void
jack_set_server_dir (const char *path)
{
	jack_server_dir = strdup (path);
}


static pthread_mutex_t client_lock;
static pthread_cond_t  client_ready;
void *jack_zero_filled_buffer = 0;

#define event_fd pollfd[0].fd
#define graph_wait_fd pollfd[1].fd

typedef struct {
    int status;
    struct _jack_client *client;
    const char *client_name;
} client_info;

char *
jack_get_shm (const char *shm_name, size_t size, int perm, int mode, int prot)
{
	int shm_fd;
	char *addr;

	if ((shm_fd = shm_open (shm_name, perm, mode)) < 0) {
		jack_error ("cannot create shm segment %s (%s)", shm_name, strerror (errno));
		return MAP_FAILED;
	}

	if (ftruncate (shm_fd, size) < 0) {
		jack_error ("cannot set size of engine shm registry (%s)", strerror (errno));
		return MAP_FAILED;
	}

	if ((addr = mmap (0, size, prot, MAP_SHARED, shm_fd, 0)) == MAP_FAILED) {
		jack_error ("cannot mmap shm segment %s (%s)", shm_name, strerror (errno));
		shm_unlink (shm_name);
		close (shm_fd);
		return MAP_FAILED;
	}

	return addr;
}

void 
jack_error (const char *fmt, ...)
{
	va_list ap;
	char buffer[300];

	va_start (ap, fmt);
	vsnprintf (buffer, sizeof(buffer), fmt, ap);
	jack_error_callback (buffer);
	va_end (ap);
}

void default_jack_error_callback (const char *desc)
{
    fprintf(stderr, "%s\n", desc);
}

void (*jack_error_callback)(const char *desc) = &default_jack_error_callback;

static int
oop_client_deliver_request (void *ptr, jack_request_t *req)
{
	jack_client_t *client = (jack_client_t*) ptr;

	if (write (client->request_fd, req, sizeof (*req)) != sizeof (*req)) {
		jack_error ("cannot send request type %d to server", req->type);
		req->status = -1;
	}
	if (read (client->request_fd, req, sizeof (*req)) != sizeof (*req)) {
		jack_error ("cannot read result for request type %d from server (%s)", req->type, strerror (errno));
		req->status = -1;
	}

	return req->status;
}

int
jack_client_deliver_request (const jack_client_t *client, jack_request_t *req)
{
	/* indirect through the function pointer that was set 
	   either by jack_client_new() (external) or handle_new_client()
	   in the server.
	*/

	return client->control->deliver_request (client->control->deliver_arg, req);
}

jack_client_t *
jack_client_alloc ()
{
	jack_client_t *client;

	client = (jack_client_t *) malloc (sizeof (jack_client_t));
	client->pollfd = (struct pollfd *) malloc (sizeof (struct pollfd) * 2);
	client->pollmax = 2;

	client->request_fd = -1;
	client->event_fd = -1;
	client->graph_wait_fd = -1;
	client->graph_next_fd = -1;
	client->port_segments = NULL;
	client->ports = NULL;
	client->engine = NULL;
	client->control = 0;
	client->thread_ok = FALSE;
	client->first_active = TRUE;
	client->on_shutdown = NULL;

	return client;
}

jack_client_t *
jack_client_alloc_internal (jack_client_control_t *cc, jack_control_t *ec)
{
	jack_client_t* client;

	client = jack_client_alloc ();
	client->control = cc;
	client->engine = ec;
	
	return client;
}

static void
jack_client_free (jack_client_t *client)
{
	if (client->pollfd) {
		free (client->pollfd);
	}

	free (client);
}

static void
jack_client_invalidate_port_buffers (jack_client_t *client)
{
	JSList *node;
	jack_port_t *port;

	/* This releases all local memory owned by input ports
	   and sets the buffer pointer to NULL. This will cause
	   jack_port_get_buffer() to reallocate space for the
	   buffer on the next call (if there is one).
	*/

	for (node = client->ports; node; node = jack_slist_next (node)) {
		port = (jack_port_t *) node->data;

		if (port->shared->flags & JackPortIsInput) {
			if (port->client_segment_base == 0) {
				jack_pool_release ((void *) port->shared->offset);
				port->client_segment_base = 0;
				port->shared->offset = 0;
			}
		}
	}
}

int
jack_client_handle_port_connection (jack_client_t *client, jack_event_t *event)

{
	jack_port_t *control_port;
	jack_port_t *other;
	JSList *node;

	switch (event->type) {
	case PortConnected:
		other = jack_port_new (client, event->y.other_id, client->engine);
		control_port = jack_port_by_id (client, event->x.self_id);
		pthread_mutex_lock (&control_port->connection_lock);
		control_port->connections = jack_slist_prepend (control_port->connections, (void*)other);
		pthread_mutex_unlock (&control_port->connection_lock);
		break;

	case PortDisconnected:
		control_port = jack_port_by_id (client, event->x.self_id);

		pthread_mutex_lock (&control_port->connection_lock);

		for (node = control_port->connections; node; node = jack_slist_next (node)) {

			other = (jack_port_t *) node->data;

			if (other->shared->id == event->y.other_id) {
				control_port->connections = jack_slist_remove_link (control_port->connections, node);
				jack_slist_free_1 (node);
				free (other);
				break;
			}
		}

		pthread_mutex_unlock (&control_port->connection_lock);
		break;

	default:
		/* impossible */
		break;
	}

	return 0;
}

static int 
jack_handle_reorder (jack_client_t *client, jack_event_t *event)
{	
	char path[PATH_MAX+1];

	if (client->graph_wait_fd >= 0) {
		DEBUG ("closing graph_wait_fd==%d", client->graph_wait_fd);
		close (client->graph_wait_fd);
		client->graph_wait_fd = -1;
	} 

	if (client->graph_next_fd >= 0) {
		DEBUG ("closing graph_next_fd==%d", client->graph_next_fd);
		close (client->graph_next_fd);
		client->graph_next_fd = -1;
	}

	sprintf (path, "%s-%lu", client->fifo_prefix, event->x.n);

	if ((client->graph_wait_fd = open (path, O_RDONLY|O_NONBLOCK)) < 0) {
		jack_error ("cannot open specified fifo [%s] for reading (%s)", path, strerror (errno));
		return -1;
	}


	DEBUG ("opened new graph_wait_fd %d (%s)", client->graph_wait_fd, path);

	sprintf (path, "%s-%lu", client->fifo_prefix, event->x.n+1);
	
	if ((client->graph_next_fd = open (path, O_WRONLY|O_NONBLOCK)) < 0) {
		jack_error ("cannot open specified fifo [%s] for writing (%s)", path, strerror (errno));
		return -1;
	}

	DEBUG ("opened new graph_next_fd %d (%s)", client->graph_next_fd, path);

	/* If the client registered its own callback for graph order events,
	   execute it now.
	*/

	if (client->control->graph_order) {
		client->control->graph_order (client->control->graph_order_arg);
	}

	return 0;
}
		
static int
server_connect (int which)
{
	int fd;
	struct sockaddr_un addr;

	if ((fd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0) {
		jack_error ("cannot create client socket (%s)", strerror (errno));
		return -1;
	}

	addr.sun_family = AF_UNIX;
	snprintf (addr.sun_path, sizeof (addr.sun_path) - 1, "%s/jack_%d", jack_server_dir, which);

	if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) < 0) {
		jack_error ("cannot connect to jack server", strerror (errno));
		close (fd);
		return -1;
	}

	return fd;
}

static int
server_event_connect (jack_client_t *client)
{
	int fd;
	struct sockaddr_un addr;
	jack_client_connect_ack_request_t req;
	jack_client_connect_ack_result_t res;

	if ((fd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0) {
		jack_error ("cannot create client event socket (%s)", strerror (errno));
		return -1;
	}

	addr.sun_family = AF_UNIX;
	snprintf (addr.sun_path, sizeof (addr.sun_path) - 1, "%s/jack_ack_0", jack_server_dir);

	if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) < 0) {
		jack_error ("cannot connect to jack server for events", strerror (errno));
		close (fd);
		return -1;
	}

	req.client_id = client->control->id;

	if (write (fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot write event connect request to server (%s)", strerror (errno));
		close (fd);
		return -1;
	}

	if (read (fd, &res, sizeof (res)) != sizeof (res)) {
		jack_error ("cannot read event connect result from server (%s)", strerror (errno));
		close (fd);
		return -1;
	}

	if (res.status != 0) {
		close (fd);
		return -1;
	}

	return fd;
}

static int
jack_request_client (ClientType type, const char* client_name, const char* so_name, 
		     const char* so_data, jack_client_connect_result_t *res, int *req_fd)
{
	jack_client_connect_request_t req;

	*req_fd = -1;

	if (strlen (client_name) > sizeof (req.name) - 1) {
		jack_error ("\"%s\" is too long to be used as a JACK client name.\n"
			     "Please use %lu characters or less.",
			    client_name, sizeof (req.name) - 1);
		return -1;
	}

	if (strlen (so_name) > sizeof (req.object_path) - 1) {
		jack_error ("\"%s\" is too long to be used as a JACK shared object name.\n"
			     "Please use %lu characters or less.",
			    so_name, sizeof (req.object_path) - 1);
		return -1;
	}

	if (strlen (so_data) > sizeof (req.object_data) - 1) {
		jack_error ("\"%s\" is too long to be used as a JACK shared object data string.\n"
			     "Please use %lu characters or less.",
			    so_data, sizeof (req.object_data) - 1);
		return -1;
	}

	if ((*req_fd = server_connect (0)) < 0) {
		jack_error ("cannot connect to default JACK server");
		goto fail;
	}

	req.load = TRUE;
	req.type = type;
	snprintf (req.name, sizeof (req.name), "%s", client_name);
	snprintf (req.object_path, sizeof (req.object_path), "%s", so_name);
	snprintf (req.object_data, sizeof (req.object_data), "%s", so_data);

	if (write (*req_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot send request to jack server (%s)", strerror (errno));
		goto fail;
	}
	
	if (read (*req_fd, res, sizeof (*res)) != sizeof (*res)) {

		if (errno == 0) {
			/* server shut the socket */
			jack_error ("could not attach as client (duplicate client name?)");
			goto fail;
		}

		jack_error ("cannot read response from jack server (%s)", strerror (errno));
		goto fail;
	}

	if (res->status) {
		jack_error ("could not attach as client (duplicate client name?)");
		goto fail;
	}

	if (res->protocol_v != jack_protocol_version){
		jack_error ("application linked against too wrong of a version of libjack.");
		goto fail;
	}

	switch (type) {
	case ClientDriver:
	case ClientInternal:
		close (*req_fd);
		*req_fd = -1;
		break;

	default:
		break;
	}

	return 0;

  fail:
	if (*req_fd >= 0) {
		close (*req_fd);
		*req_fd = -1;
	}
	return -1;
}

jack_client_t *
jack_client_new (const char *client_name)
{
	int req_fd = -1;
	int ev_fd = -1;
	jack_client_connect_result_t  res;
	jack_client_t *client;
	void *addr;

	/* external clients need this initialized; internal clients
	   will use the setup in the server's address space.
	*/

	jack_init_time ();

	if (jack_request_client (ClientExternal, client_name, "", "", &res, &req_fd)) {
		return NULL;
	}

	client = jack_client_alloc ();

	strcpy (client->fifo_prefix, res.fifo_prefix);
	client->request_fd = req_fd;

	client->pollfd[0].events = POLLIN|POLLERR|POLLHUP|POLLNVAL;
	client->pollfd[1].events = POLLIN|POLLERR|POLLHUP|POLLNVAL;

	/* attach the engine control/info block */

	if ((addr = jack_get_shm (res.control_shm_name, res.control_size, O_RDWR, 
				  0, (PROT_READ|PROT_WRITE))) == MAP_FAILED) {
		jack_error ("cannot attached engine control shared memory segment");
		goto fail;
	}

	client->engine = (jack_control_t *) addr;

	/* now attach the client control block */

	if ((addr = jack_get_shm (res.client_shm_name, sizeof (jack_client_control_t), O_RDWR, 
				  0, (PROT_READ|PROT_WRITE))) == MAP_FAILED) {
		jack_error ("cannot attached client control shared memory segment");
		goto fail;
	}

	client->control = (jack_client_control_t *) addr;

	jack_client_handle_new_port_segment (client, res.port_segment_name, res.port_segment_size, 0);

	/* set up the client so that it does the right thing for an external client */

	client->control->deliver_request = oop_client_deliver_request;
	client->control->deliver_arg = client;

	if ((ev_fd = server_event_connect (client)) < 0) {
		jack_error ("cannot connect to server for event stream (%s)", strerror (errno));
		goto fail;
	}

	client->event_fd = ev_fd;

	return client;
	
  fail:
	if (client->engine) {
		munmap ((char *) client->engine, sizeof (jack_control_t));
	}
	if (client->control) {
		munmap ((char *) client->control, sizeof (jack_client_control_t));
	}
	if (req_fd >= 0) {
		close (req_fd);
	}
	if (ev_fd >= 0) {
		close (ev_fd);
	}

	return 0;
}

int
jack_internal_client_new (const char *client_name, const char *so_name, const char *so_data)
{
	jack_client_connect_result_t res;
	int req_fd;
	
	return jack_request_client (ClientInternal, client_name, so_name, so_data, &res, &req_fd);
}

void
jack_internal_client_close (const char *client_name)
{
	jack_client_connect_request_t req;
	int fd;

	req.load = FALSE;
	snprintf (req.name, sizeof (req.name), "%s", client_name);
	
	if ((fd = server_connect (0)) < 0) {
		jack_error ("cannot connect to default JACK server.");
		return;
	}

	if (write (fd, &req, sizeof (req)) != sizeof(req)) {
		jack_error ("cannot deliver ClientUnload request to JACK server.");
	}
	
	/* no response to this request */
	
	close (fd);
	return;
}

void
jack_client_handle_new_port_segment (jack_client_t *client, shm_name_t shm_name, size_t size, void* addr)
{
	jack_port_segment_info_t *si;

	/* Lookup, attach and register the port/buffer segments in use
	   right now.
	*/

	if (client->control->type == ClientExternal) {

		
		if ((addr = jack_get_shm(shm_name, size, O_RDWR, 0, (PROT_READ|PROT_WRITE))) == MAP_FAILED) {
			jack_error ("cannot attached port segment shared memory (%s)", strerror (errno));
			return;
		}

	} else {

		/* client is in same address space as server, so just use `addr' directly */
	}

	si = (jack_port_segment_info_t *) malloc (sizeof (jack_port_segment_info_t));
	strcpy (si->shm_name, shm_name);
	si->address = addr;
	si->size = size;

	/* the first chunk of the first port segment is always set by the engine
	   to be a conveniently-sized, zero-filled lump of memory.
	*/

	if (client->port_segments == NULL) {
		jack_zero_filled_buffer = si->address;
	}

	client->port_segments = jack_slist_prepend (client->port_segments, si);
}

static void *
jack_client_thread (void *arg)

{
	jack_client_t *client = (jack_client_t *) arg;
	jack_client_control_t *control = client->control;
	jack_event_t event;
	char status = 0;
	char c;
	int err = 0;

	pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	pthread_mutex_lock (&client_lock);
	client->thread_ok = TRUE;
	client->thread_id = pthread_self();
	pthread_cond_signal (&client_ready);
	pthread_mutex_unlock (&client_lock);

	client->control->pid = getpid();

	DEBUG ("client thread is now running");

	while (err == 0) {
	        if (client->engine->engine_ok == 0) {
		     jack_error ("engine unexpectedly shutdown; thread exiting\n");
		     if (client->on_shutdown) {
			     client->on_shutdown (client->on_shutdown_arg);
		     }
		     pthread_exit (0);

		}

		DEBUG ("client polling on event_fd and graph_wait_fd...");
                
		if (poll (client->pollfd, client->pollmax, 1000) < 0) {
			if (errno == EINTR) {
				printf ("poll interrupted\n");
				continue;
			}
			jack_error ("poll failed in client (%s)", strerror (errno));
			status = -1;
			break;
		}

		/* get an accurate timestamp on waking from poll for a process()
		   cycle.
		*/

		if (client->pollfd[1].revents & POLLIN) {
			control->awake_at = jack_get_microseconds();
		}

		if (client->pollfd[0].revents & ~POLLIN || client->control->dead) {
			goto zombie;
		}

		if (client->pollfd[0].revents & POLLIN) {

			DEBUG ("client receives an event, now reading on event fd");
                
			/* server has sent us an event. process the event and reply */

			if (read (client->event_fd, &event, sizeof (event)) != sizeof (event)) {
				jack_error ("cannot read server event (%s)", strerror (errno));
				err++;
				break;
			}
			
			status = 0;

			switch (event.type) {
			case PortRegistered:
				if (control->port_register) {
					control->port_register (event.x.port_id, TRUE, control->port_register_arg);
				} 
				break;

			case PortUnregistered:
				if (control->port_register) {
					control->port_register (event.x.port_id, FALSE, control->port_register_arg);
				}
				break;

			case GraphReordered:
				status = jack_handle_reorder (client, &event);
				break;

			case PortConnected:
			case PortDisconnected:
				status = jack_client_handle_port_connection (client, &event);
				break;

			case BufferSizeChange:
				jack_client_invalidate_port_buffers (client);

				if (control->bufsize) {
					status = control->bufsize (control->nframes, control->bufsize_arg);
				} 
				break;

			case SampleRateChange:
				if (control->srate) {
					status = control->srate (control->nframes, control->srate_arg);
				}
				break;

			case XRun:
				if (control->xrun) {
					status = control->xrun (control->xrun_arg);
				}
				break;

			case NewPortBufferSegment:
				jack_client_handle_new_port_segment (client, event.x.shm_name, event.z.size, event.y.addr);
				break;
			}

			DEBUG ("client has dealt with the event, writing response on event fd");

			if (write (client->event_fd, &status, sizeof (status)) != sizeof (status)) {
				jack_error ("cannot send event response to engine (%s)", strerror (errno));
				err++;
				break;
			}
		}

		if (client->pollfd[1].revents & POLLIN) {

#ifdef WITH_TIMESTAMPS
			jack_reset_timestamps ();
#endif

			DEBUG ("client %d signalled at %Lu, awake for process at %Lu (delay = %f usecs) (wakeup on graph_wait_fd==%d)", 
			       getpid(),
			       control->signalled_at, 
			       control->awake_at, 
			       control->awake_at - control->signalled_at,
			       client->pollfd[1].fd);

			control->state = Running;

			if (control->process) {
				if (control->process (control->nframes, control->process_arg) == 0) {
					control->state = Finished;
				}
			} else {
				control->state = Finished;
			}
			
			control->finished_at = jack_get_microseconds();

#ifdef WITH_TIMESTAMPS
			jack_timestamp ("finished");
#endif
			/* pass the execution token along */

			DEBUG ("client finished processing at %Lu (elapsed = %f usecs), writing on graph_next_fd==%d", 
			       control->finished_at, 
			       ((float)(control->finished_at - control->awake_at)/client->cpu_mhz),
			       client->graph_next_fd);

			if (write (client->graph_next_fd, &c, sizeof (c)) != sizeof (c)) {
				jack_error ("cannot continue execution of the processing graph (%s)", strerror(errno));
				err++;
				break;
			}

			DEBUG ("client sent message to next stage by %Lu, client reading on graph_wait_fd==%d", 
			       jack_get_microseconds(), client->graph_wait_fd);

#ifdef WITH_TIMESTAMPS
			jack_timestamp ("read pending byte from wait");
#endif
			DEBUG("reading cleanup byte from pipe\n");

			if ((read (client->graph_wait_fd, &c, sizeof (c)) != sizeof (c))) {
				DEBUG ("WARNING: READ FAILED!");
/*
				jack_error ("cannot complete execution of the processing graph (%s)", strerror(errno));
				err++;
				break;
*/
			}

			/* check if we were killed during the process cycle (or whatever) */

			if (client->control->dead) {
				goto zombie;
			}

			DEBUG("process cycle fully complete\n");

#ifdef WITH_TIMESTAMPS
			jack_timestamp ("read done");
#endif			

		}
	}
	
	return (void *) ((intptr_t)err);

  zombie:
	if (client->on_shutdown) {
		jack_error ("zombified - calling shutdown handler");
		client->on_shutdown (client->on_shutdown_arg);
	} else {
		jack_error ("zombified - exiting from JACK");
		jack_client_close (client);
	}

	pthread_exit (0);
	/*NOTREACHED*/
	return 0;
}

static int
jack_start_thread (jack_client_t *client)

{
	pthread_attr_t *attributes = 0;
#ifdef USE_CAPABILITIES
	int policy = SCHED_OTHER;
	struct sched_param client_param, temp_param;
#endif

	if (client->engine->real_time) {

		/* Get the client thread to run as an RT-FIFO
		   scheduled thread of appropriate priority.
		*/

		struct sched_param rt_param;

		attributes = (pthread_attr_t *) malloc (sizeof (pthread_attr_t));

		pthread_attr_init (attributes);

		if (pthread_attr_setschedpolicy (attributes, SCHED_FIFO)) {
			jack_error ("cannot set FIFO scheduling class for RT thread");
			return -1;
		}

		if (pthread_attr_setscope (attributes, PTHREAD_SCOPE_SYSTEM)) {
			jack_error ("Cannot set scheduling scope for RT thread");
			return -1;
		}

		memset (&rt_param, 0, sizeof (rt_param));
		rt_param.sched_priority = client->engine->client_priority;

		if (pthread_attr_setschedparam (attributes, &rt_param)) {
			jack_error ("Cannot set scheduling priority for RT thread (%s)", strerror (errno));
			return -1;
		}

		if (mlockall (MCL_CURRENT|MCL_FUTURE)) {
			jack_error ("cannot lock down all memory (%s)", strerror (errno));
			return -1;
		}
	}

	if (pthread_create (&client->thread, attributes, jack_client_thread, client)) {
#ifdef USE_CAPABILITIES
		if (client->engine->real_time && client->engine->has_capabilities) {
			/* we are probably dealing with a broken glibc so try
			   to work around the bug, see below for more details
			*/
			goto capabilities_workaround;
		}
#endif
		return -1;
	}
	return 0;

#ifdef USE_CAPABILITIES

	/* we get here only with engine running realtime and capabilities */

 capabilities_workaround:

	/* the version of glibc I've played with has a bug that makes
	   that code fail when running under a non-root user but with the
	   proper realtime capabilities (in short,  pthread_attr_setschedpolicy 
	   does not check for capabilities, only for the uid being
	   zero). Newer versions apparently have this fixed. This
	   workaround temporarily switches the client thread to the
	   proper scheduler and priority, then starts the realtime
	   thread so that it can inherit them and finally switches the
	   client thread back to what it was before. Sigh. For ardour
	   I have to check again and switch the thread explicitly to
	   realtime, don't know why or how to debug - nando
	*/

	/* get current scheduler and parameters of the client process */
	if ((policy = sched_getscheduler (0)) < 0) {
		jack_error ("Cannot get current client scheduler: %s", strerror(errno));
		return -1;
	}
	memset (&client_param, 0, sizeof (client_param));
	if (sched_getparam (0, &client_param)) {
		jack_error ("Cannot get current client scheduler parameters: %s", strerror(errno));
		return -1;
	}

	/* temporarily change the client process to SCHED_FIFO so that
	   the realtime thread can inherit the scheduler and priority
	*/
	memset (&temp_param, 0, sizeof (temp_param));
	temp_param.sched_priority = client->engine->client_priority;
	if (sched_setscheduler(0, SCHED_FIFO, &temp_param)) {
		jack_error ("Cannot temporarily set client to RT scheduler: %s", strerror(errno));
		return -1;
	}

	/* prepare the attributes for the realtime thread */
	attributes = (pthread_attr_t *) malloc (sizeof (pthread_attr_t));
	pthread_attr_init (attributes);
	if (pthread_attr_setscope (attributes, PTHREAD_SCOPE_SYSTEM)) {
		sched_setscheduler (0, policy, &client_param);
		jack_error ("Cannot set scheduling scope for RT thread");
		return -1;
	}
	if (pthread_attr_setinheritsched (attributes, PTHREAD_INHERIT_SCHED)) {
		sched_setscheduler (0, policy, &client_param);
		jack_error ("Cannot set scheduler inherit policy for RT thread");
		return -1;
	}

	/* create the RT thread */
	if (pthread_create (&client->thread, attributes, jack_client_thread, client)) {
		sched_setscheduler (0, policy, &client_param);
		return -1;
	}

	/* return the client process to the scheduler it was in before */
	if (sched_setscheduler (0, policy, &client_param)) {
		jack_error ("Cannot reset original client scheduler: %s", strerror(errno));
		return -1;
	}

	/* check again... inheritance of policy and priority works in jack_simple_client
	   but not in ardour! So I check again and force the policy if it is not set
	   correctly. This does not really really work either, the manager thread
	   of the linuxthreads implementation is left running with SCHED_OTHER,
	   that is presumably very bad.
	*/
	memset (&client_param, 0, sizeof (client_param));
	if (pthread_getschedparam(client->thread, &policy, &client_param) == 0) {
		if (policy != SCHED_FIFO) {
			/* jack_error ("RT thread did not go SCHED_FIFO, trying again"); */
			memset (&client_param, 0, sizeof (client_param));
			client_param.sched_priority = client->engine->client_priority;
			if (pthread_setschedparam (client->thread, SCHED_FIFO, &client_param)) {
				jack_error ("Cannot set (again) FIFO scheduling class for RT thread\n");
				return -1;
			}
		}
	}
	return 0;
#endif
}

int 
jack_activate (jack_client_t *client)
{
	jack_request_t req;

	/* we need to scribble on our stack to ensure that its memory pages are
	 * actually mapped (more important for mlockall(2) usage in
	 * jack_start_thread()) 
	 */

#define BIG_ENOUGH_STACK 1048576

	char buf[BIG_ENOUGH_STACK];
	int i;

	for (i = 0; i < BIG_ENOUGH_STACK; i++) {
		buf[i] = (char) (i & 0xff);
	}

#undef BIG_ENOUGH_STACK

	if (client->control->type == ClientInternal || client->control->type == ClientDriver) {
		goto startit;
	}

	/* get the pid of the client process to pass it to engine */

	client->control->pid = getpid ();

#ifdef USE_CAPABILITIES

	if (client->engine->has_capabilities != 0 &&
	    client->control->pid != 0 && client->engine->real_time != 0) {

		/* we need to ask the engine for realtime capabilities
		   before trying to start the realtime thread
		*/

		req.type = SetClientCapabilities;
		req.x.client_id = client->control->id;
		
		jack_client_deliver_request (client, &req);

		if (req.status) {

			/* what to do? engine is running realtime, it is using capabilities and has
			   them (otherwise we would not get an error return) but for some reason it
			   could not give the client the required capabilities, so for now downgrade
			   the client so that it still runs, albeit non-realtime - nando
			*/

			jack_error ("could not receive realtime capabilities, client will run non-realtime");
			/* XXX wrong, this is a property of the engine
			client->engine->real_time = 0;
			*/
		}
	}
#endif

	if (client->first_active) {

		pthread_mutex_init (&client_lock, NULL);
		pthread_cond_init (&client_ready, NULL);
		
		pthread_mutex_lock (&client_lock);
		
		if (jack_start_thread (client)) {
			pthread_mutex_unlock (&client_lock);
			return -1;
		}

		pthread_cond_wait (&client_ready, &client_lock);
		pthread_mutex_unlock (&client_lock);
		
		if (!client->thread_ok) {
			jack_error ("could not start client thread");
			return -1;
		}

		client->first_active = FALSE;
	}

  startit:

	req.type = ActivateClient;
	req.x.client_id = client->control->id;

	return jack_client_deliver_request (client, &req);
}

int 
jack_deactivate (jack_client_t *client)

{
	jack_request_t req;

	req.type = DeactivateClient;
	req.x.client_id = client->control->id;

	return jack_client_deliver_request (client, &req);
}

int
jack_client_close (jack_client_t *client)
{
	JSList *node;
	void *status;

	if (client->control->active) {
		jack_deactivate (client);
	}

	if (client->control->type == ClientExternal) {
		/* stop the thread that communicates with the jack server */
		pthread_cancel (client->thread);
		pthread_join (client->thread, &status);

		munmap ((char *) client->control, sizeof (jack_client_control_t));
		munmap ((char *) client->engine, sizeof (jack_control_t));

		for (node = client->port_segments; node; node = jack_slist_next (node)) {
			jack_port_segment_info_t *si = (jack_port_segment_info_t *) node->data;
			munmap ((char *) si->address, si->size);
			free (node->data);
		}
		jack_slist_free (client->port_segments);

		if (client->graph_wait_fd) {
			close (client->graph_wait_fd);
		}
		
		if (client->graph_next_fd) {
			close (client->graph_next_fd);
		}
		
		close (client->event_fd);
		close (client->request_fd);
	}

	for (node = client->ports; node; node = jack_slist_next (node)) {
		free (node->data);
	}
	jack_slist_free (client->ports);
	jack_client_free (client);
	
	return 0;
}	

unsigned long jack_get_buffer_size (jack_client_t *client)

{
	return client->engine->buffer_size;
}

unsigned long jack_get_sample_rate (jack_client_t *client)

{
	return client->engine->current_time.frame_rate;
}

int 
jack_connect (jack_client_t *client, const char *source_port, const char *destination_port)

{
	jack_request_t req;

	req.type = ConnectPorts;

	snprintf (req.x.connect.source_port, sizeof (req.x.connect.source_port), "%s", source_port);
	snprintf (req.x.connect.destination_port, sizeof (req.x.connect.destination_port), "%s", destination_port);

	return jack_client_deliver_request (client, &req);
}

int
jack_port_disconnect (jack_client_t *client, jack_port_t *port)
{
	jack_request_t req;

	pthread_mutex_lock (&port->connection_lock);

	if (port->connections == NULL) {
		pthread_mutex_unlock (&port->connection_lock);
		return 0;
	}

	pthread_mutex_unlock (&port->connection_lock);

	req.type = DisconnectPort;
	req.x.port_info.port_id = port->shared->id;

	return jack_client_deliver_request (client, &req);
}

int 
jack_disconnect (jack_client_t *client, const char *source_port, const char *destination_port)
{
	jack_request_t req;

	req.type = DisconnectPorts;

	snprintf (req.x.connect.source_port, sizeof (req.x.connect.source_port), "%s", source_port);
	snprintf (req.x.connect.destination_port, sizeof (req.x.connect.destination_port), "%s", destination_port);
	
	return jack_client_deliver_request (client, &req);
}

int
jack_engine_takeover_timebase (jack_client_t *client)

{
	jack_request_t req;

	req.type = SetTimeBaseClient;
	req.x.client_id = client->control->id;

	return jack_client_deliver_request (client, &req);
}	

void
jack_set_error_function (void (*func) (const char *))
{
	jack_error_callback = func;
}


int 
jack_set_graph_order_callback (jack_client_t *client, JackGraphOrderCallback callback, void *arg)
{
	if (client->control->active) {
		jack_error ("You cannot set callbacks on an active client.");
		return -1;
	}
	client->control->graph_order = callback;
	client->control->graph_order_arg = arg;
	return 0;
}

int
jack_set_process_callback (jack_client_t *client, JackProcessCallback callback, void *arg)

{
	if (client->control->active) {
		jack_error ("You cannot set callbacks on an active client.");
		return -1;
	}
	client->control->process_arg = arg;
	client->control->process = callback;
	return 0;
}

int
jack_set_buffer_size_callback (jack_client_t *client, JackBufferSizeCallback callback, void *arg)
{
	client->control->bufsize_arg = arg;
	client->control->bufsize = callback;
	return 0;
}

int
jack_set_sample_rate_callback (jack_client_t *client, JackSampleRateCallback callback, void *arg)
{
	if (client->control->active) {
		jack_error ("You cannot set callbacks on an active client.");
		return -1;
	}
	client->control->srate_arg = arg;
	client->control->srate = callback;

	/* Now invoke it */

	callback (client->engine->current_time.frame_rate, arg);

	return 0;
}

int
jack_set_port_registration_callback(jack_client_t *client, JackPortRegistrationCallback callback, void *arg)

{
	if (client->control->active) {
		jack_error ("You cannot set callbacks on an active client.");
		return -1;
	}
	client->control->port_register_arg = arg;
	client->control->port_register = callback;
	return 0;
}

int
jack_get_process_start_fd (jack_client_t *client)
{
	/* once this has been called, the client thread
	   does not sleep on the graph wait fd.
	*/

	client->pollmax = 1;
	return client->graph_wait_fd;

}

int
jack_get_process_done_fd (jack_client_t *client)
{
	return client->graph_next_fd;
}


void
jack_on_shutdown (jack_client_t *client, void (*function)(void *arg), void *arg)
{
	client->on_shutdown = function;
	client->on_shutdown_arg = arg;
}

const char **
jack_get_ports (jack_client_t *client,
		const char *port_name_pattern,
		const char *type_name_pattern,
		unsigned long flags)
{
	jack_control_t *engine;
	const char **matching_ports;
	unsigned long match_cnt;
	jack_port_shared_t *psp;
	unsigned long i;
	regex_t port_regex;
	regex_t type_regex;
	int matching;

	engine = client->engine;

	if (port_name_pattern && port_name_pattern[0]) {
		regcomp (&port_regex, port_name_pattern, REG_EXTENDED|REG_NOSUB);
	}
	if (type_name_pattern && type_name_pattern[0]) {
		regcomp (&type_regex, type_name_pattern, REG_EXTENDED|REG_NOSUB);
	}

	psp = engine->ports;
	match_cnt = 0;

	matching_ports = (const char **) malloc (sizeof (char *) * engine->port_max);

	for (i = 0; i < engine->port_max; i++) {
		matching = 1;

		if (!psp[i].in_use) {
			continue;
		}

		if (flags) {
			if ((psp[i].flags & flags) != flags) {
				matching = 0;
			}
		}

		if (matching && port_name_pattern && port_name_pattern[0]) {
			if (regexec (&port_regex, psp[i].name, 0, NULL, 0)) {
				matching = 0;
			}
		} 

		if (matching && type_name_pattern && type_name_pattern[0]) {
			if (regexec (&type_regex, psp[i].type_info.type_name, 0, NULL, 0)) {
				matching = 0;
			}
		} 
		
		if (matching) {
			matching_ports[match_cnt++] = psp[i].name;
		}
	}

	matching_ports[match_cnt] = 0;

	if (match_cnt == 0) {
		free (matching_ports);
		matching_ports = 0;
	}

	return matching_ports;
}

static inline void
jack_read_frame_time (const jack_client_t *client, jack_frame_timer_t *copy)
{
	int tries = 0;

	do {
		/* throttle the busy wait if we don't get 
		   the answer very quickly.
		*/

		if (tries > 10) {
			usleep (20);
			tries = 0;
		}

		*copy = client->engine->frame_timer;

		tries++;

	} while (copy->guard1 != copy->guard2);
}

jack_nframes_t
jack_frames_since_cycle_start (const jack_client_t *client)
{
	float usecs;

	usecs = jack_get_microseconds() - client->engine->current_time.usecs;
	return (jack_nframes_t) floor ((((float) client->engine->current_time.frame_rate) / 1000000.0f) * usecs);
}

jack_nframes_t
jack_frame_time (const jack_client_t *client)
{
	jack_frame_timer_t current;
	float usecs;
	jack_nframes_t elapsed;

	jack_read_frame_time (client, &current);
	
	usecs = jack_get_microseconds() - current.stamp;
	elapsed = (jack_nframes_t) floor ((((float) client->engine->current_time.frame_rate) / 1000000.0f) * usecs);
	
	return current.frames + elapsed;
}


/* TRANSPORT CONTROL */

int
jack_get_transport_info (jack_client_t *client,
			 jack_transport_info_t *info)
{
	jack_time_info_t *time_info = &client->engine->current_time;

	if (info->valid & JackTransportState) {
		info->state = time_info->transport_state;
	}
	
	if (info->valid & JackTransportPosition) {
		info->position = time_info->frame;
	}

	if (info->valid & JackTransportLoop) {
		info->loop_start = time_info->loop_start;
		info->loop_end = time_info->loop_end;
	}

	return 0;
}

int
jack_set_transport_info (jack_client_t *client,
			 jack_transport_info_t *info)
{
	jack_time_info_t *time_info = &client->engine->pending_time;
	
	if (info->valid & JackTransportState) {
		time_info->transport_state = info->state;
	}

	if (info->valid & JackTransportPosition) {
		time_info->frame = info->position;
	}

	if (info->valid & JackTransportLoop) {
		time_info->loop_start = info->loop_start;
		time_info->loop_end = info->loop_end;
	}

	return 0;
}	

float
jack_cpu_load (jack_client_t *client)
{
	return client->engine->cpu_load;
}

pthread_t
jack_client_thread_id (jack_client_t *client)
{
	return client->thread_id;
}

#if defined(linux)

jack_time_t
jack_get_mhz (void)
{
	FILE *f = fopen("/proc/cpuinfo", "r");
	if (f == 0)
	{
		perror("can't open /proc/cpuinfo\n");
		exit(1);
	}

	for ( ; ; )
	{
		jack_time_t mhz;
		int ret;
		char buf[1000];

		if (fgets(buf, sizeof(buf), f) == NULL)
		{
			fprintf(stderr, "cannot locate cpu MHz in /proc/cpuinfo\n");
			exit(1);
		}

#ifdef __powerpc__
		ret = sscanf(buf, "clock\t: %LuMHz", &mhz);
#else
		ret = sscanf(buf, "cpu MHz         : %Lu", &mhz);
#endif /* __powerpc__ */

		if (ret == 1)
		{
			fclose(f);
			return mhz;
		}
	}
}

void jack_init_time ()
{
	__jack_cpu_mhz = jack_get_mhz ();
}

#endif
