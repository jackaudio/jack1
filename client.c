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
#include <sys/mman.h>
#include <sys/poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <regex.h>
#include <math.h>
#include <asm/msr.h>

#include <jack/jack.h>
#include <jack/internal.h>
#include <jack/engine.h>
#include <jack/pool.h>
#include <jack/error.h>

char *jack_temp_dir = "/tmp";

void
jack_set_temp_dir (const char *path)
{
	jack_temp_dir = strdup (path);
}

static jack_port_t *jack_port_new (jack_client_t *client, jack_port_id_t port_id, jack_control_t *control);

static pthread_mutex_t client_lock;
static pthread_cond_t  client_ready;
void *jack_zero_filled_buffer = 0;

static void jack_audio_port_mixdown (jack_port_t *port, nframes_t nframes);

jack_port_type_info_t builtin_port_types[] = {
	{ JACK_DEFAULT_AUDIO_TYPE, jack_audio_port_mixdown, 1 },
	{ "", NULL }
};

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
    char fifo_prefix[PATH_MAX+1];
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

jack_port_t *
jack_port_by_name (jack_client_t *client, const char *port_name)
{
	unsigned long i, limit;
	jack_port_shared_t *port;
	
	limit = client->engine->port_max;
	port = &client->engine->ports[0];
	
	for (i = 0; i < limit; i++) {
		if (port[i].in_use && strcmp (port[i].name, port_name) == 0) {
			return jack_port_new (client, port[i].id, client->engine);
		}
	}

	return NULL;
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
	GSList *node;

	switch (event->type) {
	case PortConnected:
		other = jack_port_new (client, event->y.other_id, client->engine);
		control_port = jack_port_by_id (client, event->x.self_id);
		pthread_mutex_lock (&control_port->connection_lock);
		control_port->connections = g_slist_prepend (control_port->connections, other);
		pthread_mutex_unlock (&control_port->connection_lock);
		break;

	case PortDisconnected:
		control_port = jack_port_by_id (client, event->x.self_id);

		pthread_mutex_lock (&control_port->connection_lock);

		for (node = control_port->connections; node; node = g_slist_next (node)) {

			other = (jack_port_t *) node->data;

			if (other->shared->id == event->y.other_id) {
				control_port->connections = g_slist_remove_link (control_port->connections, node);
				g_slist_free_1 (node);
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
	g_snprintf (addr.sun_path, sizeof (addr.sun_path) - 1, "%s/jack_%d", jack_temp_dir, which);

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
	g_snprintf (addr.sun_path, sizeof (addr.sun_path) - 1, "%s/jack_ack_0", jack_temp_dir);

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

		if (errno == 0) {
			/* server shut the socket */
			jack_error ("could not attach as client (duplicate client name?)");
			close (req_fd);
			return NULL;
		}

		jack_error ("cannot read response from jack server (%s)", strerror (errno));
		close (req_fd);
		return NULL;
	}

	if (res.status) {
		close (req_fd);
		jack_error ("could not attach as client (duplicate client name?)");
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
		jack_error ("cannot determine shared memory segment for port segment key 0x%x (%s)", res.port_segment_key, strerror (errno));
		goto fail;
	}

	if ((addr = shmat (port_segment_shm_id, 0, 0)) == (void *) -1) {
		jack_error ("cannot attached port segment shared memory (%s)", strerror (errno));
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

			if (read (client->graph_wait_fd, &c, sizeof (c)) != sizeof (c)) {
				jack_error ("cannot clean up byte from inter-client pipe (%s)", strerror (errno));
				err++;
				break;
			}

			control->state = Running;

			if (control->process) {
				if (control->process (control->nframes, control->process_arg) == 0) {
					control->state = Finished;
				}
			} else {
				control->state = Finished;
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

		if (mlockall (MCL_CURRENT|MCL_FUTURE)) {
			jack_error ("cannot lock down all memory (%s)", strerror (errno));
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
	void *status;

	/* stop the thread that communicates with the jack server */

	pthread_cancel (client->thread);
	pthread_join (client->thread, &status);

	shmdt ((char *) client->control);
	shmdt (client->engine);

	for (node = client->port_segments; node; node = g_slist_next (node)) {
		shmdt (((jack_port_segment_info_t *) node->data)->address);
		free (node->data);
	}
	g_slist_free (client->port_segments);

	for (node = client->ports; node; node = g_slist_next (node)) {
		free (node->data);
	}
	g_slist_free (client->ports);

	if (client->graph_wait_fd) {
		close (client->graph_wait_fd);
	}

	if (client->graph_next_fd) {
		close (client->graph_next_fd);
	}

	close (client->event_fd);
	close (client->request_fd);

	free (client->pollfd);
	free (client);

	return 0;
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
	int port_segment_shm_id;
	jack_port_segment_info_t *si;
	void *addr;

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

	/* Lookup, attach and register the port/buffer segments in use
	   right now.
	*/

	if ((port_segment_shm_id = shmget (res.port_segment_key, 0, 0)) < 0) {
		jack_error ("cannot determine shared memory segment for port segment key 0x%x (%s)", res.port_segment_key, strerror (errno));
		return NULL;
	}

	if ((addr = shmat (port_segment_shm_id, 0, 0)) == (void *) -1) {
		jack_error ("cannot attached port segment shared memory (%s)", strerror (errno));
		return NULL;
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
	return client->engine->time.frame_rate;
}

static jack_port_t *
jack_port_new (jack_client_t *client, jack_port_id_t port_id, jack_control_t *control)

{
	jack_port_t *port;
	jack_port_shared_t *shared;
	jack_port_segment_info_t *si;
	GSList *node;

	shared = &control->ports[port_id];

	port = (jack_port_t *) malloc (sizeof (jack_port_t));

	port->client_segment_base = 0;
	port->shared = shared;
	pthread_mutex_init (&port->connection_lock, NULL);
	port->connections = 0;
	port->tied = NULL;

	si = NULL;

	for (node = client->port_segments; node; node = g_slist_next (node)) {

		si = (jack_port_segment_info_t *) node->data;

		if (si->shm_key == port->shared->shm_key) {
			break;
		}
	}
	
	if (si == NULL) {
		jack_error ("cannot find port segment to match newly registered port\n");
		return NULL;
	}

	port->client_segment_base = si->address;

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
	jack_port_type_info_t *type_info;
	int n;

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

	port = jack_port_new (client, req.x.port_info.port_id, client->engine);

	type_info = NULL;

	for (n = 0; builtin_port_types[n].type_name[0]; n++) {
		
		if (strcmp (req.x.port_info.type, builtin_port_types[n].type_name) == 0) {
			type_info = &builtin_port_types[n];
			break;
		}
	}

	if (type_info == NULL) {
		
		/* not a builtin type, so allocate a new type_info structure,
		   and fill it appropriately.
		*/
		
		type_info = (jack_port_type_info_t *) malloc (sizeof (jack_port_type_info_t));

		snprintf ((char *) type_info->type_name, sizeof (type_info->type_name), req.x.port_info.type);

		type_info->mixdown = NULL;            /* we have no idea how to mix this */
		type_info->buffer_scale_factor = -1;  /* use specified port buffer size */
	} 

	memcpy (&port->shared->type_info, type_info, sizeof (jack_port_type_info_t));

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
jack_connect (jack_client_t *client, const char *source_port, const char *destination_port)

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

	if (write (client->request_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot send port disconnect request to server");
		return -1;
	}

	if (read (client->request_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot read port disconnect result from server");
		return -1;
	}
	
	return req.status;
}

int 
jack_disconnect (jack_client_t *client, const char *source_port, const char *destination_port)
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

nframes_t
jack_port_get_latency (jack_port_t *port)
{
	return port->shared->latency;
}

void
jack_port_set_latency (jack_port_t *port, nframes_t nframes)
{
	port->shared->latency = nframes;
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
		return jack_port_buffer (port);
	}

	/* Input port. 
	*/

	/* since this can only be called from the process() callback,
	   and since no connections can be made/broken during this
	   phase (enforced by the jack server), there is no need
	   to take the connection lock here
	*/

	if ((node = port->connections) == NULL) {
		
		/* no connections; return a zero-filled buffer */

		return jack_zero_filled_buffer;
	}

	if ((next = g_slist_next (node)) == NULL) {

		/* one connection: use zero-copy mode - just pass
		   the buffer of the connected (output) port.
		*/

		return jack_port_buffer (((jack_port_t *) node->data));
	}

	/* multiple connections. use a local buffer and mixdown
	   the incoming data to that buffer. we have already
	   established the existence of a mixdown function
	   during the connection process.

	   no port can have an offset of 0 - that offset refers
	   to the zero-filled area at the start of a shared port
	   segment area. so, use the offset to store the location
	   of a locally allocated buffer, and reset the client_segment_base 
	   so that the jack_port_buffer() computation works correctly.
	*/

	if (port->shared->offset == 0) {
		port->shared->offset = (size_t) jack_pool_alloc (port->shared->type_info.buffer_scale_factor * 
								 sizeof (sample_t) * nframes);
		port->client_segment_base = 0;
	}

	port->shared->type_info.mixdown (port, nframes);
	return (sample_t *) port->shared->offset;
}

int
jack_port_tie (jack_port_t *src, jack_port_t *dst)

{
	if (dst->shared->client_id != src->shared->client_id) {
		jack_error ("cannot tie ports not owned by the same client");
		return -1;
	}

	if (dst->shared->flags & JackPortIsOutput) {
		jack_error ("cannot tie an input port");
		return -1;
	}

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
	port->tied = NULL;
	return 0;
}

int 
jack_set_graph_order_callback (jack_client_t *client, JackGraphOrderCallback callback, void *arg)
{
	if (client->control->active) {
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

	callback (client->engine->time.frame_rate, arg);

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
jack_port_request_monitor_by_name (jack_client_t *client, const char *port_name, int onoff)

{
	jack_port_t *port;
	unsigned long i, limit;
	jack_port_shared_t *ports;

	limit = client->engine->port_max;
	ports = &client->engine->ports[0];
	
	for (i = 0; i < limit; i++) {
		if (ports[i].in_use && strcmp (ports[i].name, port_name) == 0) {
			port = jack_port_new (client, ports[i].id, client->engine);
			return jack_port_request_monitor (port, onoff);
			free (port);
			return 0;
		}
	}

	return -1;
}

int 
jack_port_request_monitor (jack_port_t *port, int onoff)

{
	if (onoff) {
		port->shared->monitor_requests++;
	} else if (port->shared->monitor_requests) {
		port->shared->monitor_requests--;
	}

	if ((port->shared->flags & JackPortIsOutput) == 0) {

		GSList *node;

		/* this port is for input, so recurse over each of the 
		   connected ports.
		 */

		pthread_mutex_lock (&port->connection_lock);
		for (node = port->connections; node; node = g_slist_next (node)) {
			
			/* drop the lock because if there is a feedback loop,
			   we will deadlock. XXX much worse things will
			   happen if there is a feedback loop !!!
			*/

			pthread_mutex_unlock (&port->connection_lock);
			jack_port_request_monitor ((jack_port_t *) node->data, onoff);
			pthread_mutex_lock (&port->connection_lock);
		}
		pthread_mutex_unlock (&port->connection_lock);
	}

	return 0;
}
	
int
jack_ensure_port_monitor_input (jack_port_t *port, int yn)
{
	if (yn) {
		if (port->shared->monitor_requests == 0) {
			port->shared->monitor_requests++;
		}
	} else {
		if (port->shared->monitor_requests == 1) {
			port->shared->monitor_requests--;
		}
	}

	return 0;
}

int
jack_port_monitoring_input (jack_port_t *port)
{
	return port->shared->monitor_requests > 0;
}

const char *
jack_port_name (const jack_port_t *port)
{
	return port->shared->name;
}

const char *
jack_port_short_name (const jack_port_t *port)
{
	/* we know there is always a colon, because we put
	   it there ...
	*/

	return strchr (port->shared->name, ':') + 1;
}

int
jack_port_flags (const jack_port_t *port)
{
	return port->shared->flags;
}

const char *
jack_port_type (const jack_port_t *port)
{
	return port->shared->type_info.type_name;
}

int
jack_port_set_name (jack_port_t *port, const char *new_name)
{
	char *colon;
	int len;

	colon = strchr (port->shared->name, ':');
	len = sizeof (port->shared->name) - ((int) (colon - port->shared->name)) - 2;
	snprintf (colon+1, len, "%s", new_name);
	
	return 0;
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

nframes_t
jack_frames_since_cycle_start (jack_client_t *client)
{
	struct timeval now;
	float usecs;

	gettimeofday (&now, NULL);
	usecs = ((now.tv_sec * 1000000) + now.tv_usec) - client->engine->time.microseconds;

	return (nframes_t) floor ((((float) client->engine->time.frame_rate) / 1000000.0f) * usecs);
}

int
jack_port_lock (jack_client_t *client, jack_port_t *port)
{
	if (port) {
		port->shared->locked = 1;
		return 0;
	}
	return -1;
}

int
jack_port_unlock (jack_client_t *client, jack_port_t *port)
{
	if (port) {
		port->shared->locked = 0;
		return 0;
	}
	return -1;
}

static void 
jack_audio_port_mixdown (jack_port_t *port, nframes_t nframes)
{
	GSList *node;
	jack_port_t *input;
	nframes_t n;
	sample_t *buffer;
	sample_t *dst, *src;

	/* by the time we've called this, we've already established
	   the existence of more than 1 connection to this input port.
	*/

	/* no need to take connection lock, since this is called
	   from the process() callback, and the jack server
	   ensures that no changes to connections happen
	   during this time.
	*/

	node = port->connections;
	input = (jack_port_t *) node->data;
	buffer = jack_port_buffer (port);

	memcpy (buffer, jack_port_buffer (input), sizeof (sample_t) * nframes);

	for (node = g_slist_next (node); node; node = g_slist_next (node)) {

		input = (jack_port_t *) node->data;

		n = nframes;
		dst = buffer;
		src = jack_port_buffer (input);

		while (n--) {
			*dst++ += *src++;
		}
	}
}

const char **
jack_port_get_connections (const jack_port_t *port)
{
	const char **ret;
	GSList *node;
	unsigned int n;

	pthread_mutex_lock (&((jack_port_t *) port)->connection_lock);

	if (port->connections == NULL) {
		pthread_mutex_unlock (&((jack_port_t *) port)->connection_lock);
		return NULL;
	}

	ret = (const char **) malloc (sizeof (char *) * (g_slist_length (port->connections) + 1));

	for (n = 0, node = port->connections; node; node = g_slist_next (node), n++) {
		ret[n] = ((jack_port_t *) node->data)->shared->name;
	}

	ret[n] = NULL;

	pthread_mutex_unlock (&((jack_port_t *) port)->connection_lock);
	
	return ret;
}

int
jack_port_connected (const jack_port_t *port)
{
	return port->connections != NULL;
}

int
jack_port_connected_to (const jack_port_t *port, const char *portname)
{
	GSList *node;
	int ret = FALSE;

	pthread_mutex_lock (&((jack_port_t *) port)->connection_lock);

	for (node = port->connections; node; node = g_slist_next (node)) {
		jack_port_t *other_port = (jack_port_t *) node->data;
		
		if (strcmp (other_port->shared->name, portname) == 0) {
			ret = TRUE;
			break;
		}
	}

	pthread_mutex_unlock (&((jack_port_t *) port)->connection_lock);
	return ret;
}

int
jack_port_connected_to_port (const jack_port_t *port, const jack_port_t *other_port)
{
	GSList *node;
	int ret = FALSE;
	
	pthread_mutex_lock (&((jack_port_t *) port)->connection_lock);
	
	for (node = port->connections; node; node = g_slist_next (node)) {

		jack_port_t *this_port = (jack_port_t *) node->data;
		
		if (other_port->shared == this_port->shared) {
			ret = TRUE;
			break;
		}
	}

	pthread_mutex_unlock (&((jack_port_t *) port)->connection_lock);
	return ret;
}

/* TRANSPORT CONTROL */

int
jack_get_transport_info (jack_client_t *client,
			 jack_transport_info_t *info)
{
	jack_time_info_t *time_info = &client->engine->time;
	
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
	jack_time_info_t *time_info = &client->engine->time;
	
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

nframes_t
jack_port_get_total_latency (jack_client_t *client, jack_port_t *port)
{
	return port->shared->total_latency;
}
