/*
    Copyright (C) 2001 Paul Davis
    
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
#include <sys/shm.h>
#include <sys/poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <asm/msr.h>

#include <jack/jack.h>
#include <jack/internal.h>
#include <jack/engine.h>
#include <jack/pool.h>
#include <jack/error.h>

static pthread_mutex_t client_lock;
static pthread_cond_t  client_ready;
static void *jack_zero_filled_buffer = 0;

struct _jack_client {

    jack_control_t        *engine;
    jack_client_control_t *control;
    struct pollfd *pollfd;
    int pollmax;
    int graph_next_fd;
    int request_fd;
    GSList *port_segments;
    GSList *ports;
    pthread_t thread;
    char fifo_prefix[FIFO_NAME_SIZE+1];
    void (*on_shutdown)(void *arg);
    void *on_shutdown_arg;
    char thread_ok : 1;
    char first_active : 1;
};

#define event_fd pollfd[0].fd
#define graph_wait_fd pollfd[1].fd

typedef struct {
    int status;
    struct _jack_client *client;
    const char *client_name;
} client_info;

static void 
default_jack_error (const char *fmt, ...)

{
	va_list ap;

	va_start (ap, fmt);
	vfprintf (stderr, fmt, ap);
	va_end (ap);
	fputc ('\n', stderr);
}

void (*jack_error)(const char *fmt, ...) = &default_jack_error;

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

static jack_port_shared_t *
jack_port_shared_by_id (jack_client_t *client, jack_port_id_t id)

{
	return &client->engine->ports[id];
}

static jack_port_t *
jack_port_by_id (jack_client_t *client, jack_port_id_t id)

{
	GSList *node;

	for (node = client->ports; node; node = g_slist_next (node)) {
		if (((jack_port_t *) node->data)->shared->id == id) {
			return (jack_port_t *) node->data;
		}
	}

	return NULL;
}

static jack_port_id_t
jack_port_id_by_name (jack_client_t *client, const char *port_name)

{
	jack_port_id_t id, limit;
	jack_port_shared_t *port;

	limit = client->engine->port_max;
	port = &client->engine->ports[0];
	
	for (id = 0; id < limit; id++) {
		if (port[id].in_use && strcmp (port[id].name, port_name) == 0) {
			return port[id].id;
		}
	}

	return NoPort;
}

static void
jack_client_invalidate_port_buffers (jack_client_t *client)

{
	GSList *node;
	jack_port_t *port;

	/* This releases all local memory owned by input ports
	   and sets the buffer pointer to NULL. This will cause
	   jack_port_get_buffer() to reallocate space for the
	   buffer on the next call (if there is one).
	*/

	for (node = client->ports; node; node = g_slist_next (node)) {
		port = (jack_port_t *) node->data;

		if (port->shared->flags & JackPortIsInput) {
			/* XXX release buffer */
			port->shared->buffer = NULL;
		}
	}
}

int
jack_client_handle_port_connection (jack_client_t *client, jack_event_t *event)

{
	jack_port_t *control_port;
	jack_port_shared_t *shared;
	GSList *node;

	switch (event->type) {
	case PortConnected:
		shared = jack_port_shared_by_id (client, event->y.other_id);
		control_port = jack_port_by_id (client, event->x.self_id);
		control_port->connections = g_slist_prepend (control_port->connections, shared);
		printf ("%s connected to %s\n", control_port->shared->name, shared->name);
		break;

	case PortDisconnected:
		shared = jack_port_shared_by_id (client, event->y.other_id);
		control_port = jack_port_by_id (client, event->x.self_id);

		for (node = control_port->connections; node; node = g_slist_next (node)) {
			if (((jack_port_shared_t *) node->data) == shared) {
				control_port->connections = g_slist_remove_link (control_port->connections, node);
				g_slist_free_1 (node);
				break;
			}
		}
		printf ("%s DIS-connected and %s\n", control_port->shared->name, shared->name);
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
	char path[FIFO_NAME_SIZE+1];

	if (client->graph_wait_fd >= 0) {
		close (client->graph_wait_fd);
		client->graph_wait_fd = -1;
	} 

	if (client->graph_next_fd >= 0) {
		close (client->graph_next_fd);
		client->graph_next_fd = -1;
	}

	sprintf (path, "%s-%lu", client->fifo_prefix, event->x.n);

	if ((client->graph_wait_fd = open (path, O_RDONLY)) <= 0) {
		jack_error ("cannot open specified fifo [%s] for reading (%s)", path, strerror (errno));
		return -1;
	}

	sprintf (path, "%s-%lu", client->fifo_prefix, event->x.n+1);
	
	if ((client->graph_next_fd = open (path, O_WRONLY)) < 0) {
		jack_error ("cannot open specified fifo [%s] for writing (%s)", path, strerror (errno));
		return -1;
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
	g_snprintf (addr.sun_path, sizeof (addr.sun_path) - 1, "/tmp/jack_%d", which);

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
	g_snprintf (addr.sun_path, sizeof (addr.sun_path) - 1, "/tmp/jack_ack_0");

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

jack_client_t *
jack_client_new (const char *client_name)

{
	int req_fd = -1;
	int ev_fd = -1;
	void *addr;
	jack_client_connect_request_t req;
	jack_client_connect_result_t  res;
	jack_port_segment_info_t *si;
	jack_client_t *client;
	int client_shm_id;
	int control_shm_id;
	int port_segment_shm_id;
	int n;

	if (strlen (client_name) > sizeof (req.name) - 1) {
		jack_error ("\"%s\" is too long to be used as a JACK client name.\n"
			     "Please use %lu characters or less.",
			     sizeof (req.name) - 1);
		return NULL;
	}

	if ((req_fd = server_connect (0)) < 0) {
		jack_error ("cannot connect to default JACK server");
		return NULL;
	}

	req.type = ClientOutOfProcess;
	strncpy (req.name, client_name, sizeof (req.name) - 1);
	
	if (write (req_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot send request to jack server (%s)", strerror (errno));
		close (req_fd);
		return NULL;
	}
	
	if ((n = read (req_fd, &res, sizeof (res))) != sizeof (res)) {
		jack_error ("cannot read response from jack server (%s)", strerror (errno));
		close (req_fd);
		return NULL;
	}

	if (res.status) {
		close (req_fd);
		jack_error ("could not attach as client");
		return NULL;
	}

	client = jack_client_alloc ();

	strcpy (client->fifo_prefix, res.fifo_prefix);
	client->request_fd = req_fd;

	client->pollfd[0].events = POLLIN|POLLERR|POLLHUP|POLLNVAL;
	client->pollfd[1].events = POLLIN|POLLERR|POLLHUP|POLLNVAL;

	/* Lookup, attach and register the port/buffer segments in use
	   right now.
	*/

	if ((port_segment_shm_id = shmget (res.port_segment_key, 0, 0)) < 0) {
		jack_error ("cannot determine shared memory segment for port segment key 0x%x", res.port_segment_key);
		goto fail;
	}

	if ((addr = shmat (port_segment_shm_id, res.port_segment_address, 0)) == (void *) -1) {
		jack_error ("cannot attached port segment shared memory at 0x%", res.port_segment_address);
		goto fail;
	}

	si = (jack_port_segment_info_t *) malloc (sizeof (jack_port_segment_info_t));
	si->shm_key = res.port_segment_key;
	si->address = addr;

	/* the first chunk of the first port segment is always set by the engine
	   to be a conveniently-sized, zero-filled lump of memory.
	*/

	if (client->port_segments == NULL) {
		jack_zero_filled_buffer = si->address;
	}

	client->port_segments = g_slist_prepend (client->port_segments, si);

	/* attach the engine control/info block */

	if ((control_shm_id = shmget (res.control_key, 0, 0)) < 0) {
		jack_error ("cannot determine shared memory segment for control key 0x%x", res.control_key);
		goto fail;
	}

	if ((addr = shmat (control_shm_id, 0, 0)) == (void *) -1) {
		jack_error ("cannot attached engine control shared memory segment");
		goto fail;
	}

	client->engine = (jack_control_t *) addr;

	/* now attach the client control block */

	if ((client_shm_id = shmget (res.client_key, 0, 0)) < 0) {
		jack_error ("cannot determine shared memory segment for client key 0x%x", res.client_key);
		goto fail;
	}

	if ((addr = shmat (client_shm_id, 0, 0)) == (void *) -1) {
		jack_error ("cannot attached client control shared memory segment");
		goto fail;
	}

	client->control = (jack_client_control_t *) addr;

	if ((ev_fd = server_event_connect (client)) < 0) {
		jack_error ("cannot connect to server for event stream (%s)", strerror (errno));
		goto fail;
	}

	client->event_fd = ev_fd;

	return client;
	
  fail:
	if (client->engine) {
		shmdt (client->engine);
	}
	if (client->control) {
		shmdt ((char *) client->control);
	}
	if (req_fd >= 0) {
		close (req_fd);
	}
	if (ev_fd >= 0) {
		close (ev_fd);
	}

	return 0;
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
	pthread_cond_signal (&client_ready);
	pthread_mutex_unlock (&client_lock);

	while (err == 0) {

		if (poll (client->pollfd, client->pollmax, 1000) < 0) {
			if (errno == EINTR) {
				printf ("poll interrupted\n");
				continue;
			}
			jack_error ("poll failed in client (%s)", strerror (errno));
			status = -1;
			break;
		}

		if (client->pollfd[0].revents & ~POLLIN) {
			jack_error ("engine has shut down socket; thread exiting");
			if (client->on_shutdown) {
				client->on_shutdown (client->on_shutdown_arg);
			}
			pthread_exit (0);
		}

		if (client->pollfd[0].revents & POLLIN) {
			
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

			case NewPortBufferSegment:
				break;

			case PortMonitor:
				if (control->port_monitor) {
					control->port_monitor (event.x.port_id, TRUE, control->port_monitor_arg);
				}
				break;

			case PortUnMonitor:
				if (control->port_monitor) {
					control->port_monitor (event.x.port_id, FALSE, control->port_monitor_arg);
				}
				break;

			}

			if (write (client->event_fd, &status, sizeof (status)) != sizeof (status)) {
				jack_error ("cannot send event response to engine (%s)", strerror (errno));
				err++;
				break;
			}

		}

		if (client->pollfd[1].revents & POLLIN) {

			/* the previous stage of the graph has told us to 
			   process().
			*/

			control->state = JACK_CLIENT_STATE_TRIGGERED;

			if (read (client->graph_wait_fd, &c, sizeof (c)) != sizeof (c)) {
				jack_error ("cannot clean up byte from inter-client pipe (%s)", strerror (errno));
				err++;
				break;
			}

                        status = control->process (control->nframes, control->process_arg);
			
			if (!status) {
				control->state = JACK_CLIENT_STATE_FINISHED;
			}

			/* this may fail. if it does, the engine will discover
			   it due a cycle timeout, which is about
			   the best we can do without a lot of mostly wasted
			   effort.
			*/

			write (client->graph_next_fd, &c, 1);
		}
	}
	
	return (void *) err;
}

static int
jack_start_thread (jack_client_t *client)

{
	pthread_attr_t *attributes = 0;

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
	}

	if (pthread_create (&client->thread, attributes, jack_client_thread, client)) {
		return -1;
	}
	return 0;
}

int 
jack_activate (jack_client_t *client)

{
	jack_request_t req;

	if (client->control->type == ClientOutOfProcess && client->first_active) {

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

	req.type = ActivateClient;
	req.x.client_id = client->control->id;

	if (write (client->request_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot send activate client request to server");
		return -1;
	}

	if (read (client->request_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot read activate client result from server (%s)", strerror (errno));
		return -1;
	}

	return req.status;
}

int 
jack_deactivate (jack_client_t *client)

{
	jack_request_t req;

	req.type = DeactivateClient;
	req.x.client_id = client->control->id;

	if (write (client->request_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot send activate client request to server");
		return -1;
	}

	if (read (client->request_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot read activate client result from server (%s)", strerror (errno));
		return -1;
	}

	return req.status;
}

int
jack_client_close (jack_client_t *client)

{
	GSList *node;
	jack_request_t req;
	void *status;

	req.type = DropClient;
	req.x.client_id = client->control->id;

	/* stop the thread */

	pthread_cancel (client->thread);
	pthread_join (client->thread, &status);

	shmdt ((char *) client->control);
	shmdt (client->engine);

	for (node = client->port_segments; node; node = g_slist_next (node)) {
		jack_port_segment_info_t *si;
		si = (jack_port_segment_info_t *) node->data;
		shmdt (si->address);
		free (si);
	}

	g_slist_free (client->port_segments);
	g_slist_free (client->ports);

	if (client->graph_wait_fd) {
		close (client->graph_wait_fd);
	}

	if (client->graph_next_fd) {
		close (client->graph_next_fd);
	}

	if (write (client->request_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot send drop client request to server");
		req.status = -1;
	} 

	close (client->event_fd);
	close (client->request_fd);

	free (client->pollfd);
	free (client);

	return req.status;
}	

int
jack_load_client (const char *client_name, const char *path_to_so)

{
	int fd;
	jack_client_connect_request_t req;
	jack_client_connect_result_t res;

	if ((fd = server_connect (0)) < 0) {
		jack_error ("cannot connect to jack server");
		return 0;
	}

	req.type = ClientDynamic;

	strncpy (req.name, client_name, sizeof (req.name) - 1);
	req.name[sizeof(req.name)-1] = '\0';
	strncpy (req.object_path, path_to_so, sizeof (req.name) - 1);
	req.object_path[sizeof(req.object_path)-1] = '\0';
	
	if (write (fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot send request to jack server (%s)", strerror (errno));
		close (fd);
		return 0;
	}

	if (read (fd, &res, sizeof (res)) != sizeof (res)) {
		jack_error ("cannot read response from jack server (%s)", strerror (errno));
		close (fd);
		return 0;
	}

	close (fd);
	return res.status;
}

jack_client_t *
jack_driver_become_client (const char *client_name)

	
{
	int fd;
	jack_client_connect_request_t req;
	jack_client_connect_result_t res;
	jack_client_t *client = 0;

	if ((fd = server_connect (0)) < 0) {
		jack_error ("cannot connect to jack server");
		return 0;
	}

	req.type = ClientDriver;
	strncpy (req.name, client_name, sizeof (req.name) - 1);
	req.name[sizeof(req.name)-1] = '\0';

	if (write (fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot send request to jack server (%s)", strerror (errno));
		close (fd);
		return 0;
	}

	if (read (fd, &res, sizeof (res)) != sizeof (res)) {
		jack_error ("cannot read response from jack server (%s)", strerror (errno));
		close (fd);
		return 0;
	}

	if (res.status) {
		return 0;
	}

	client = jack_client_alloc ();

	client->request_fd = fd;
	client->control = res.client_control;
	client->engine = res.engine_control;

	/* allow the engine to act on the client's behalf
	   when dealing with in-process clients.
	*/

	client->control->private_internal_client = client;

	return client;
}

unsigned long jack_get_buffer_size (jack_client_t *client)

{
	return client->engine->buffer_size;
}

unsigned long jack_get_sample_rate (jack_client_t *client)

{
	return client->engine->sample_rate;
}

static jack_port_t *
jack_port_new (jack_port_id_t port_id, jack_control_t *control)

{
	jack_port_t *port;
	jack_port_shared_t *shared;

	shared = &control->ports[port_id];

	port = (jack_port_t *) malloc (sizeof (jack_port_t));

	port->shared = shared;
	port->connections = 0;
	port->tied = NULL;

	return port;
}

jack_port_t *
jack_port_register (jack_client_t *client, 
		     const char *port_name,
		     const char *port_type,
		     unsigned long flags,
		     unsigned long buffer_size)
{
	jack_request_t req;
	jack_port_t *port = 0;

	/* before we get started, check a few basics */

	if (flags & JackPortCanMonitor) {
		if (client->control->port_monitor == NULL) {
			jack_error ("you cannot register ports with PortCanMonitor "
				     "without a port monitor callback");
			return NULL;
		}
	}

	req.type = RegisterPort;

	strcpy ((char *) req.x.port_info.name, (const char *) client->control->name);
	strcat ((char *) req.x.port_info.name, ":");
	strcat ((char *) req.x.port_info.name, port_name);

	strncpy (req.x.port_info.type, port_type, sizeof (req.x.port_info.type) - 1);
	req.x.port_info.flags = flags;
	req.x.port_info.buffer_size = buffer_size;
	req.x.port_info.client_id = client->control->id;

	if (write (client->request_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot send port registration request to server");
		return 0;
	}

	if (read (client->request_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot read port registration result from server");
		return 0;
	}
	
	if (req.status != 0) {
		return NULL;
	}

	port = jack_port_new (req.x.port_info.port_id, client->engine);
	client->ports = g_slist_prepend (client->ports, port);

	return port;
}

int 
jack_port_unregister (jack_client_t *client, jack_port_t *port)

{
	jack_request_t req;

	req.type = UnRegisterPort;
	req.x.port_info.port_id = port->shared->id;
	req.x.port_info.client_id = client->control->id;

	if (write (client->request_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot send port registration request to server");
		return -1;
	}

	if (read (client->request_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot read port registration result from server");
		return -1;
	}
	
	return req.status;
}

int 
jack_port_connect (jack_client_t *client, const char *source_port, const char *destination_port)

{
	jack_request_t req;

	req.type = ConnectPorts;

	strncpy (req.x.connect.source_port, source_port, sizeof (req.x.connect.source_port) - 1);
	req.x.connect.source_port[sizeof(req.x.connect.source_port) - 1] = '\0';
	strncpy (req.x.connect.destination_port, destination_port, sizeof (req.x.connect.destination_port) - 1);
	req.x.connect.destination_port[sizeof(req.x.connect.destination_port) - 1] = '\0';

	if (write (client->request_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot send port connection request to server");
		return -1;
	}

	if (read (client->request_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot read port connection result from server");
		return -1;
	}

	return req.status;
}

int 
jack_port_disconnect (jack_client_t *client, const char *source_port, const char *destination_port)

{
	jack_request_t req;

	req.type = DisconnectPorts;

	strncpy (req.x.connect.source_port, source_port, sizeof (req.x.connect.source_port) - 1);
	req.x.connect.source_port[sizeof(req.x.connect.source_port) - 1] = '\0';
	strncpy (req.x.connect.destination_port, destination_port, sizeof (req.x.connect.destination_port) - 1);
	req.x.connect.destination_port[sizeof(req.x.connect.destination_port) - 1] = '\0';

	if (write (client->request_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot send port connection request to server");
		return -1;
	}

	if (read (client->request_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot read port connection result from server");
		return -1;
	}
	
	return req.status;
}

int
jack_engine_takeover_timebase (jack_client_t *client)

{
	jack_request_t req;

	req.type = SetTimeBaseClient;
	req.x.client_id = client->control->id;

	if (write (client->request_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot send set time base request to server");
		return -1;
	}

	if (read (client->request_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot read set time base result from server");
		return -1;
	}
	
	return req.status;
}	

void
jack_update_time (jack_client_t *client, nframes_t time)

{
	client->control->frame_time = time;
}

void
jack_set_error_function (void (*func) (const char *, ...))
{
	jack_error = func;
}

void *
jack_port_get_buffer (jack_port_t *port, nframes_t nframes)

{
	GSList *node, *next;

	/* Output port. The buffer was assigned by the engine
	   when the port was registered.
	*/

	if (port->shared->flags & JackPortIsOutput) {
		if (port->tied) {
			return jack_port_get_buffer (port->tied, nframes);
		}
		return port->shared->buffer;
	}

	/* Input port. 
	*/

	if ((node = port->connections) == NULL) {
		
		/* no connections; return a zero-filled buffer */

		return jack_zero_filled_buffer;
	}

	if ((next = g_slist_next (node)) == NULL) {

		/* one connection: use zero-copy mode - just pass
		   the buffer of the connected (output) port.
		*/

		return ((jack_port_shared_t *) node->data)->buffer;
	}

	/* multiple connections. use a local buffer and mixdown
	   the incoming data to that buffer. we have already
	   established the existence of a mixdown function
	   during the connection process.
	*/

	if (port->shared->buffer == NULL) {
		port->shared->buffer = jack_pool_alloc 
			(port->shared->type_info.buffer_scale_factor * sizeof (sample_t) * nframes);
	}

	port->shared->type_info.mixdown (port, nframes);

	return port->shared->buffer;
}

int
jack_port_tie (jack_port_t *dst, jack_port_t *src)

{
	if (dst->shared->client_id != src->shared->client_id) {
		jack_error ("cannot tie ports not owned by the same client");
		return -1;
	}

	if (dst->shared->flags & JackPortIsOutput) {
		jack_error ("cannot tie an input port");
		return -1;
	}

	dst->own_buffer = dst->shared->buffer;
	dst->tied = src;
	return 0;
}

int
jack_port_untie (jack_port_t *port)

{
	if (port->tied == NULL) {
		jack_error ("port \"%s\" is not tied", port->shared->name);
		return -1;
	}
	port->shared->buffer = port->own_buffer;
	port->tied = NULL;
	return 0;
}

int
jack_set_process_callback (jack_client_t *client, JackProcessCallback callback, void *arg)

{
	if (client->control->active) {
		return -1;
	}
	client->control->process_arg = arg;
	client->control->process = callback;
	return 0;
}

int
jack_set_buffer_size_callback (jack_client_t *client, JackBufferSizeCallback callback, void *arg)

{
	if (client->control->active) {
		return -1;
	}
	client->control->bufsize_arg = arg;
	client->control->bufsize = callback;

	/* Now invoke it */

	callback (client->engine->buffer_size, arg);

	return 0;
}

int
jack_set_sample_rate_callback (jack_client_t *client, JackSampleRateCallback callback, void *arg)

{
	if (client->control->active) {
		return -1;
	}
	client->control->srate_arg = arg;
	client->control->srate = callback;

	/* Now invoke it */

	callback (client->engine->sample_rate, arg);

	return 0;
}

int
jack_set_port_registration_callback(jack_client_t *client, JackPortRegistrationCallback callback, void *arg)

{
	if (client->control->active) {
		return -1;
	}
	client->control->port_register_arg = arg;
	client->control->port_register = callback;
	return 0;
}

int
jack_set_port_monitor_callback (jack_client_t *client, JackPortMonitorCallback callback, void *arg)

{
	if (client->control->active) {
		return -1;
	}
	client->control->port_monitor_arg = arg;
	client->control->port_monitor = callback;
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

int 
jack_port_request_monitor (jack_client_t *client, const char *port_name, int onoff)

{
	jack_request_t req;
	int n;

	req.type = (onoff ? RequestPortMonitor : RequestPortUnMonitor);
	req.x.port_info.port_id = jack_port_id_by_name (client, port_name);

	if (write (client->request_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot send request to jack server (%s)", strerror (errno));
		return -1;
	}
	
	if ((n = read (client->request_fd, &req, sizeof (req))) != sizeof (req)) {
		jack_error ("cannot read response from jack server (%s)", strerror (errno));
		return -1;
	}

	return req.status;
}
	
const char *
jack_port_name (const jack_port_t *port)
{
	return port->shared->name;
}

void
jack_on_shutdown (jack_client_t *client, void (*function)(void *arg), void *arg)
{
	client->on_shutdown = function;
	client->on_shutdown_arg = arg;
}
