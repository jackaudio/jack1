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

#include <unistd.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/ipc.h>
#include <signal.h>
#include <sys/types.h>
#include <string.h>
#include <limits.h>
#include <sys/mman.h>

#include <asm/msr.h>

#include <jack/internal.h>
#include <jack/engine.h>
#include <jack/driver.h>

typedef struct {

    jack_port_internal_t *source;
    jack_port_internal_t *destination;

} jack_connection_internal_t;

typedef struct _jack_client_internal {

    jack_client_control_t *control;

    int      request_fd;
    int      event_fd;
    int      subgraph_start_fd;
    int      subgraph_wait_fd;
    GSList  *ports;    /* protected by engine->graph_lock */
    GSList  *fed_by;   /* protected by engine->graph_lock */
    int      shm_id;
    int      shm_key;
    unsigned long rank;
    struct _jack_client_internal *next_client; /* not a linked list! */
    dlhandle handle;
    
} jack_client_internal_t;

static int                    jack_port_assign_buffer (jack_engine_t *, jack_port_internal_t *);
static jack_port_internal_t *jack_get_port_by_name (jack_engine_t *, const char *name);

static void jack_client_delete (jack_engine_t *, jack_client_internal_t *);
static void jack_remove_client (jack_engine_t *engine, jack_client_internal_t *client);

static jack_client_internal_t *jack_client_internal_new (jack_engine_t *engine, int fd, jack_client_connect_request_t *);
static jack_client_internal_t *jack_client_internal_by_id (jack_engine_t *engine, jack_client_id_t id);

static void jack_sort_graph (jack_engine_t *engine, int take_lock);
static int  jack_rechain_graph (jack_engine_t *engine, int take_lock);
static int  jack_get_fifo_fd (jack_engine_t *engine, int which_fifo);
static int  jack_create_fifo (jack_engine_t *engine, int which_fifo);
static int  jack_port_do_connect (jack_engine_t *engine, const char *source_port, const char *destination_port);
static int  jack_port_do_disconnect (jack_engine_t *engine, const char *source_port, const char *destination_port);

static int  jack_port_do_unregister (jack_engine_t *engine, jack_request_t *);
static int  jack_port_do_register (jack_engine_t *engine, jack_request_t *);
static void jack_port_release (jack_engine_t *engine, jack_port_internal_t *);
static void jack_port_clear_connections (jack_engine_t *engine, jack_port_internal_t *port);
static int  jack_port_disconnect_internal (jack_engine_t *engine, jack_port_internal_t *src, 
					    jack_port_internal_t *dst, int sort_graph);

static void jack_port_registration_notify (jack_engine_t *, jack_port_id_t, int);
static int  jack_send_connection_notification (jack_engine_t *, jack_client_id_t, jack_port_id_t, jack_port_id_t, int);
static int  jack_deliver_event (jack_engine_t *, jack_client_internal_t *, jack_event_t *);

static void jack_audio_port_mixdown (jack_port_t *port, nframes_t nframes);


jack_port_type_info_t builtin_port_types[] = {
	{ JACK_DEFAULT_AUDIO_TYPE, jack_audio_port_mixdown, 1 },
	{ 0, NULL }
};

static inline int 
jack_client_is_inprocess (jack_client_internal_t *client)
{
	return (client->control->type == ClientDynamic) || (client->control->type == ClientDriver);
}

static
void shm_destroy (int status, void *arg)

{
	int shm_id = (int) arg;
	shmctl (shm_id, IPC_RMID, 0);
}

static 
void unlink_path (int status, void *arg)
{
	char *path = (char *) arg;
	unlink (path);
	free (arg);
}

static int
make_sockets (int fd[2])
{
	struct sockaddr_un addr;
	int i;

	/* First, the master server socket */

	if ((fd[0] = socket (AF_UNIX, SOCK_STREAM, 0)) < 0) {
		jack_error ("cannot create server socket (%s)", strerror (errno));
		return -1;
	}

	addr.sun_family = AF_UNIX;
	for (i = 0; i < 999; i++) {
		snprintf (addr.sun_path, sizeof (addr.sun_path) - 1, "/tmp/jack_%d", i);
		if (access (addr.sun_path, F_OK) != 0) {
			break;
		}
	}

	if (i == 999) {
		jack_error ("all possible server socket names in use!!!");
		close (fd[0]);
		return -1;
	}

	on_exit (unlink_path, (void *) strdup (addr.sun_path));

	if (bind (fd[0], (struct sockaddr *) &addr, sizeof (addr)) < 0) {
		jack_error ("cannot bind server to socket (%s)", strerror (errno));
		close (fd[0]);
		return -1;
	}

	if (listen (fd[0], 1) < 0) {
		jack_error ("cannot enable listen on server socket (%s)", strerror (errno));
		close (fd[0]);
		return -1;
	}

	/* Now the client/server event ack server socket */

	if ((fd[1] = socket (AF_UNIX, SOCK_STREAM, 0)) < 0) {
		jack_error ("cannot create event ACK socket (%s)", strerror (errno));
		close (fd[0]);
		return -1;
	}

	addr.sun_family = AF_UNIX;
	for (i = 0; i < 999; i++) {
		snprintf (addr.sun_path, sizeof (addr.sun_path) - 1, "/tmp/jack_ack_%d", i);
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

	on_exit (unlink_path, (void *) strdup (addr.sun_path));

	if (bind (fd[1], (struct sockaddr *) &addr, sizeof (addr)) < 0) {
		jack_error ("cannot bind server to socket (%s)", strerror (errno));
		close (fd[0]);
		close (fd[1]);
		return -1;
	}

	if (listen (fd[1], 1) < 0) {
		jack_error ("cannot enable listen on server socket (%s)", strerror (errno));
		close (fd[0]);
		close (fd[1]);
		return -1;
	}

	return 0;
}

static void
jack_cleanup_clients (jack_engine_t *engine)

{
	jack_client_control_t *ctl;
	jack_client_internal_t *client;
	GSList *node;
	GSList *remove = 0;
	static int x = 0;

	x++;

	pthread_mutex_lock (&engine->graph_lock); 

	for (node = engine->clients; node; node = g_slist_next (node)) {
		
		client = (jack_client_internal_t *) node->data;
		ctl = client->control;
		
		printf ("client %s state = %d\n", ctl->name, ctl->state);
		
		if (ctl->state > JACK_CLIENT_STATE_NOT_TRIGGERED) {
			remove = g_slist_prepend (remove, node->data);
			printf ("%d: removing failed client %s\n", x, ctl->name);
		}
	}
	pthread_mutex_unlock (&engine->graph_lock);
	
	if (remove) {
		for (node = remove; node; node = g_slist_next (node)) {
			jack_remove_client (engine, (jack_client_internal_t *) node->data);
		}
		g_slist_free (remove);
	}
}	

static int
jack_add_port_segment (jack_engine_t *engine, unsigned long nports)

{
	jack_port_segment_info_t *si;
	key_t key;
	int id;
	char *addr;
	int offset;
	size_t size;
	size_t step;

	key = random();
	size = nports * sizeof (sample_t) * engine->control->buffer_size;

	if ((id = shmget (key, size, IPC_CREAT|0666)) < 0) {
		jack_error ("cannot create new port segment of %d bytes, key = 0x%x (%s)", size, key, strerror (errno));
		return -1;
	}
	
	if ((addr = shmat (id, 0, 0)) == (char *) -1) {
		jack_error ("cannot attach new port segment (%s)", strerror (errno));
		shmctl (id, IPC_RMID, 0);
		return -1;
	}
		
	on_exit (shm_destroy, (void *) id);
		
	si = (jack_port_segment_info_t *) malloc (sizeof (jack_port_segment_info_t));
	si->shm_key = key;
	si->address = addr;

	engine->port_segments = g_slist_prepend (engine->port_segments, si);
	engine->port_segment_key = key; /* XXX fix me */
	engine->port_segment_address = addr; /* XXX fix me */

	pthread_mutex_lock (&engine->buffer_lock);

	offset = 0;

	step = engine->control->buffer_size * sizeof (sample_t);

	while (offset < size) {
		jack_port_buffer_info_t *bi;

		bi = (jack_port_buffer_info_t *) malloc (sizeof (jack_port_buffer_info_t));
		bi->shm_key = key;
		bi->offset = offset;

		/* we append because we want the list to be in memory-address order */

		engine->port_buffer_freelist = g_slist_append (engine->port_buffer_freelist, bi);

		offset += step;
	}

	/* convert the first chunk of the segment into a zero-filled area */

	if (engine->silent_buffer == 0) {
		engine->silent_buffer = (jack_port_buffer_info_t *) engine->port_buffer_freelist->data;

		engine->port_buffer_freelist = g_slist_remove_link (engine->port_buffer_freelist, engine->port_buffer_freelist);

		memset (engine->port_segment_address + engine->silent_buffer->offset, 0, 
			sizeof (sample_t) * engine->control->buffer_size);
	}

	pthread_mutex_unlock (&engine->buffer_lock);

	/* XXX notify all clients of new segment */

	return 0;
}

static int
jack_set_buffer_size (jack_engine_t *engine, nframes_t nframes)
{
	/* XXX this is not really right, since it only works for
	   audio ports.
	*/

	engine->control->buffer_size = nframes;
	jack_add_port_segment (engine, engine->control->port_max);
	return 0;
}

static int
jack_set_sample_rate (jack_engine_t *engine, nframes_t nframes)

{
	engine->control->sample_rate = nframes;
	return 0;
}

static int
jack_process (jack_engine_t *engine, nframes_t nframes)
{
	int err = 0;
	jack_client_internal_t *client;
	jack_client_control_t *ctl;
	GSList *node;
	struct pollfd pollfd[1];
	char c;

	unsigned long then, now;
//	rdtscl (then);

	if (pthread_mutex_trylock (&engine->graph_lock) != 0) {
		return 0;
	}

	for (node = engine->clients; node; node = g_slist_next (node)) {
		ctl = ((jack_client_internal_t *) node->data)->control;
		ctl->state = JACK_CLIENT_STATE_NOT_TRIGGERED;
		ctl->nframes = nframes;
	}

	if (engine->timebase_client) {
		engine->control->frame_time = engine->timebase_client->control->frame_time;
	} 

	for (node = engine->clients; err == 0 && node; ) {

		client = (jack_client_internal_t *) node->data;

		if (!client->control->active) {
			node = g_slist_next (node);
			continue;
		}

		ctl = client->control;

		if (jack_client_is_inprocess (client)) {

			/* in-process client ("plugin") */

			if (ctl->process (nframes, ctl->process_arg) == 0) {
				ctl->state = JACK_CLIENT_STATE_FINISHED;
			} else {
				jack_error ("in-process client %s failed", client->control->name);
				ctl->state = JACK_CLIENT_STATE_TRIGGERED;
				err++;
				break;
			}

			node = g_slist_next (node);

		} else {

			/* out of process subgraph */

			if (write (client->subgraph_start_fd, &c, sizeof (c)) != sizeof (c)) {
				jack_error ("cannot initiate graph processing (%s)", strerror (errno));
				err++;
				break;
			} 

			/* now wait for the result. use poll instead of read so that we 
			   can timeout effectively.
			 */

			pollfd[0].fd = client->subgraph_wait_fd;
			pollfd[0].events = POLLIN|POLLERR|POLLHUP|POLLNVAL;

			rdtscl (then);
			if (poll (pollfd, 1, engine->driver->period_interval) < 0) {
				jack_error ("engine cannot poll for graph completion (%s)", strerror (errno));
				err++;
				break;
			}
			rdtscl (now);
			
			if (pollfd[0].revents == 0) {
				jack_error ("subgraph starting at %s timed out (state = %d) (time = %f usecs)", 
					     client->control->name, client->control->state,
					    ((float)(now - then))/450.0f);
				err++;
				break;
			} else if (pollfd[0].revents & ~POLLIN) {
				jack_error ("error/hangup on graph wait fd");
				err++;
				break;
			} else {
				if (read (client->subgraph_wait_fd, &c, sizeof (c)) != sizeof (c)) {
					jack_error ("cannot clean up byte from graph wait fd (%s)", strerror (errno));
					err++;
					break;
				}
			}

			/* Move to next in-process client (or end of client list) */

			while (node) {
				if (jack_client_is_inprocess (((jack_client_internal_t *) node->data))) {
					break;
				}
				node = g_slist_next (node);
			}
		}
	}
	pthread_mutex_unlock (&engine->graph_lock);
	
	if (err) {
		jack_cleanup_clients (engine);
	} 

//	rdtscl (now);
//	printf ("engine cycle time: %.6f usecs\n", ((float) (now - then)) / 450.00f);
	return 0;
}

static int
jack_load_client (jack_engine_t *engine, jack_client_internal_t *client, const char *path_to_so)
{
	const char *errstr;
	dlhandle handle;

	handle = dlopen (path_to_so, RTLD_NOW|RTLD_GLOBAL);
	
	if (handle == 0) {
		if ((errstr = dlerror ()) != 0) {
			jack_error ("can't load \"%s\": %s", path_to_so, errstr);
		} else {
			jack_error ("bizarre error loading driver shared object %s", path_to_so);
		}
		return -1;
	}

	client->handle = handle;

#if 0
	initialize = dlsym (handle, "client_initialize");

	if ((errstr = dlerror ()) != 0) {
		jack_error ("no initialize function in shared object %s\n", path_to_so);
		dlclose (handle);
		return -1;
	}

	finish = dlsym (handle, "client_finish");

	if ((errstr = dlerror ()) != 0) {
		jack_error ("no finish function in in shared driver object %s", path_to_so);
		dlclose (handle);
		return -1;
	}
#endif

	return 0;

}

static void
jack_client_unload (jack_client_internal_t *client)
{
	if (client->handle) {
//		client->finish (client);
		dlclose (client->handle);
	}
}

static int
handle_new_client (jack_engine_t *engine, int client_fd)

{
	jack_client_internal_t *client;
	jack_client_connect_request_t req;
	jack_client_connect_result_t res;

	if (read (client_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot read connection request from client");
		return -1;
	}

	res.status = 0;
	
	if ((client = jack_client_internal_new (engine, client_fd, &req)) == 0) {
		jack_error ("cannot create new client object");
		return -1;
	}

	printf ("new client: %s, type %d @ %p\n", client->control->name, req.type, client->control);

	res.status = 0;
	res.client_key = client->shm_key;
	res.control_key = engine->control_key;
	res.port_segment_key = engine->port_segment_key;
	res.realtime = engine->control->real_time;
	res.realtime_priority = engine->rtpriority - 1;

	if (jack_client_is_inprocess (client)) {

		res.client_control = client->control;
		res.engine_control = engine->control;

	} else {
		strcpy (res.fifo_prefix, engine->fifo_prefix);
	}

	res.status = 0;

	if (write (client->request_fd, &res, sizeof (res)) != sizeof (res)) {
		jack_error ("cannot write connection response to client");
		jack_client_delete (engine, client);
		return -1;
	}

	if (res.status) {
		return res.status;
	}

	pthread_mutex_lock (&engine->graph_lock);
	engine->clients = g_slist_prepend (engine->clients, client);
	pthread_mutex_unlock (&engine->graph_lock);

	if (client->control->type != ClientDynamic) {
		if (engine->pfd_max >= engine->pfd_size) {
			engine->pfd = (struct pollfd *) realloc (engine->pfd, sizeof (struct pollfd) * engine->pfd_size + 16);
			engine->pfd_size += 16;
		}
		
		engine->pfd[engine->pfd_max].fd = client->request_fd;
		engine->pfd[engine->pfd_max].events = POLLIN|POLLPRI|POLLERR|POLLHUP|POLLNVAL;
		engine->pfd_max++;
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
	
	if ((client = jack_client_internal_by_id (engine, req.client_id)) == NULL) {
		jack_error ("unknown client ID in ACK connection request");
		return -1;
	}

	fprintf (stderr, "client %s is on event fd %d\n", client->control->name, client_fd);

	client->event_fd = client_fd;

	res.status = 0;

	if (write (client->event_fd, &res, sizeof (res)) != sizeof (res)) {
		jack_error ("cannot write ACK connection response to client");
		return -1;
	}

	return 0;
}

static int
jack_client_drop (jack_engine_t *engine, jack_client_id_t id)

{
	jack_client_internal_t *client;

	if ((client = jack_client_internal_by_id (engine, id)) == 0) {
		jack_error ("unknown client ID in DropClient request");
		return -1;
	}

	jack_remove_client (engine, client);
	return 0;
}

#if 0
static int
jack_client_has_connections (jack_client_internal_t *client)

{
	GSList *node;

	for (node = client->ports; node; node = g_slist_next (node)) {
		if (((jack_port_internal_t *) node->data)->connections) {
			return TRUE;
		}
	}

	return FALSE;
}
#endif

static int
jack_client_activate (jack_engine_t *engine, jack_client_id_t id)

{
	jack_client_internal_t *client;
	GSList *node;
	int ret = -1;
	
	pthread_mutex_lock (&engine->graph_lock);

	for (node = engine->clients; node; node = g_slist_next (node)) {

		if (((jack_client_internal_t *) node->data)->control->id == id) {
		       
			client = (jack_client_internal_t *) node->data;

			if (!jack_client_is_inprocess (client)) {
				jack_create_fifo (engine, ++engine->external_client_cnt);
			} 

			client->control->active = TRUE;

			jack_rechain_graph (engine, FALSE);

			ret = 0;
			break;
		}
	}

	pthread_mutex_unlock (&engine->graph_lock);
	return ret;
}	

static int
jack_client_do_deactivate (jack_engine_t *engine, jack_client_internal_t *client)

{
	/* called must hold engine->graph_lock and must have checked for and/or
	   cleared all connections held by client.
	*/
	
	client->control->active = FALSE;
	
	if (!jack_client_is_inprocess (client)) {
		engine->external_client_cnt--;
	}
	
	jack_sort_graph (engine, FALSE);
	return 0;
}

static void
jack_client_disconnect (jack_engine_t *engine, jack_client_internal_t *client)

{
	GSList *node;
	jack_port_internal_t *port;

	/* call tree **** MUST HOLD *** engine->graph_lock */

	for (node = client->ports; node; node = g_slist_next (node)) {
		port = (jack_port_internal_t *) node->data;
		jack_port_clear_connections (engine, port);
		jack_port_release (engine, port);
	}

	g_slist_free (client->ports);
	g_slist_free (client->fed_by);
	client->fed_by = 0;
	client->ports = 0;
}			

static int
jack_client_deactivate (jack_engine_t *engine, jack_client_id_t id, int to_wait)

{
	GSList *node;
	int ret = -1;

	pthread_mutex_lock (&engine->graph_lock);

	for (node = engine->clients; node; node = g_slist_next (node)) {

		jack_client_internal_t *client = (jack_client_internal_t *) node->data;

		if (client->control->id == id) {
			
			if (client == engine->timebase_client) {
				engine->timebase_client = 0;
				engine->control->frame_time = 0;
			}
			
			jack_client_disconnect (engine, client);
			ret = jack_client_do_deactivate (engine, node->data);
			break;
		}
	}

	pthread_mutex_unlock (&engine->graph_lock);

	return ret;
}	

static int
jack_set_timebase (jack_engine_t *engine, jack_client_id_t client)
{
	int ret = -1;

	pthread_mutex_lock (&engine->graph_lock);

	if ((engine->timebase_client = jack_client_internal_by_id (engine, client)) != 0) {
		engine->control->frame_time = engine->timebase_client->control->frame_time;
		ret = 0;
	}
	pthread_mutex_unlock (&engine->graph_lock);
	return ret;
}

static int
handle_client_jack_error (jack_engine_t *engine, int fd)

{
	jack_client_internal_t *client = 0;
	GSList *node;

	pthread_mutex_lock (&engine->graph_lock);

	for (node = engine->clients; node; node = g_slist_next (node)) {
		if (((jack_client_internal_t *) node->data)->request_fd == fd) {
			client = (jack_client_internal_t *) node->data;
			break;
		}
	}

	pthread_mutex_unlock (&engine->graph_lock);

	if (client == 0) {
		jack_error ("i/o error on unknown client fd %d", fd);
		return -1;
	} 

	jack_remove_client (engine, client);
	return 0;
}

static int
jack_client_port_monitor (jack_engine_t *engine, jack_port_id_t port_id, int onoff)

{
	jack_port_shared_t *port;
	jack_client_internal_t *client = NULL;
	jack_event_t event;
	
	if (port_id < 0 || port_id >= engine->port_max) {
		jack_error ("illegal port ID in port monitor request");
		return -1;
	}

	port = &engine->control->ports[port_id];

	if (!(port->flags & JackPortCanMonitor)) {
		jack_error ("port monitor request made on a port (%s) that doesn't support monitoring",
			     port->name);
		return -1;
	}

	pthread_mutex_lock (&engine->graph_lock);
	if ((client = jack_client_internal_by_id (engine, port->client_id)) == NULL) {
		jack_error ("unknown client owns port %d!!", port_id);
		pthread_mutex_unlock (&engine->graph_lock);
		return -1;
	}
	pthread_mutex_unlock (&engine->graph_lock);

	event.type = (onoff ? PortMonitor : PortUnMonitor);
	event.x.port_id = port_id;

	return jack_deliver_event (engine, client, &event);
}

static int
handle_client_io (jack_engine_t *engine, int fd)

{
	jack_request_t req;
	jack_client_internal_t *client = 0;
	int reply_fd;
	GSList *node;

	pthread_mutex_lock (&engine->graph_lock);

	for (node = engine->clients; node; node = g_slist_next (node)) {
		if (((jack_client_internal_t *) node->data)->request_fd == fd) {
			client = (jack_client_internal_t *) node->data;
			break;
		}
	}

	pthread_mutex_unlock (&engine->graph_lock);

	if (client == 0) {
		jack_error ("client input on unknown fd %d!", fd);
		return -1;
	}

	if (read (client->request_fd, &req, sizeof (req)) < sizeof (req)) {
		jack_error ("cannot read request from client");
		jack_remove_client (engine, client);
		return -1;
	}

	reply_fd = client->request_fd;

	switch (req.type) {
	case RegisterPort:
		req.status = jack_port_do_register (engine, &req);
		break;

	case UnRegisterPort:
		req.status = jack_port_do_unregister (engine, &req);
		break;

	case ConnectPorts:
		req.status = jack_port_do_connect (engine, req.x.connect.source_port, req.x.connect.destination_port);
		break;

	case DisconnectPorts:
		req.status = jack_port_do_disconnect (engine, req.x.connect.source_port, req.x.connect.destination_port);
		break;

	case DropClient:
		req.status = jack_client_drop (engine, req.x.client_id);
		reply_fd = -1;
		break;

	case ActivateClient:
		req.status = jack_client_activate (engine, req.x.client_id);
		break;

	case DeactivateClient:
		req.status = jack_client_deactivate (engine, req.x.client_id, TRUE);
		break;

	case SetTimeBaseClient:
		req.status = jack_set_timebase (engine, req.x.client_id);
		break;

	case RequestPortMonitor:
		req.status = jack_client_port_monitor (engine, req.x.port_info.port_id, TRUE);
		break;

	case RequestPortUnMonitor:
		req.status = jack_client_port_monitor (engine, req.x.port_info.port_id, FALSE);
		break;
	}

	if (reply_fd >= 0) {
		if (write (reply_fd, &req, sizeof (req)) < sizeof (req)) {
			jack_error ("cannot write request result to client");
			return -1;
		}
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
	
	pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	engine->pfd[0].fd = engine->fds[0];
	engine->pfd[0].events = POLLIN|POLLERR;
	engine->pfd[1].fd = engine->fds[1];
	engine->pfd[1].events = POLLIN|POLLERR;
	engine->pfd_max = 2;

	while (!done) {

		/* XXX race here with new external clients
		   causing engine->pfd to be reallocated.
		   I don't know how to solve this
		   short of copying the entire
		   contents of the pfd struct. Ick.
		*/

		max = engine->pfd_max;
		pfd = engine->pfd;
	
		if (poll (pfd, max, 10000) < 0) {
			if (errno == EINTR) {
				continue;
			}
			jack_error ("poll failed (%s)", strerror (errno));
			break;
		}

		/* check the master server socket */

		if (pfd[0].revents & POLLERR) {
			jack_error ("error on server socket");
			break;
		}

		if (pfd[0].revents & POLLIN) {

			memset (&client_addr, 0, sizeof (client_addr));
			client_addrlen = sizeof (client_addr);

			if ((client_socket = accept (engine->fds[0], (struct sockaddr *) &client_addr, &client_addrlen)) < 0) {
				jack_error ("cannot accept new connection (%s)", strerror (errno));
			} else if (handle_new_client (engine, client_socket) < 0) {
				jack_error ("cannot complete new client connection process");
				close (client_socket);
			}
		}

		/* check the ACK server socket */

		if (pfd[1].revents & POLLERR) {
			jack_error ("error on server ACK socket");
			break;
		}

		if (pfd[1].revents & POLLIN) {

			memset (&client_addr, 0, sizeof (client_addr));
			client_addrlen = sizeof (client_addr);

			if ((client_socket = accept (engine->fds[1], (struct sockaddr *) &client_addr, &client_addrlen)) < 0) {
				jack_error ("cannot accept new ACK connection (%s)", strerror (errno));
			} else if (handle_client_ack_connection (engine, client_socket)) {
				jack_error ("cannot complete client ACK connection process");
				close (client_socket);
			}
		}

		/* check each client socket */

		for (i = 2; i < max; i++) {
			if (pfd[i].fd < 0) {
				continue;
			}

			if (pfd[i].revents & ~POLLIN) {
				handle_client_jack_error (engine, pfd[i].fd);
			} else if (pfd[i].revents & POLLIN) {
				if (handle_client_io (engine, pfd[i].fd)) {
					jack_error ("bad hci\n");
				}
			}
		}
	}

	return 0;
}

static void
jack_start_server (jack_engine_t *engine)

{
	pthread_create (&engine->server_thread, 0, &jack_server_thread, engine);
	pthread_detach (engine->server_thread);
}

jack_engine_t *
jack_engine_new (int realtime, int rtpriority)
{
	jack_engine_t *engine;
	size_t control_size;
	void *addr;
	int i;

	engine = (jack_engine_t *) malloc (sizeof (jack_engine_t));

	engine->driver = 0;
	engine->process = jack_process;
	engine->set_sample_rate = jack_set_sample_rate;
	engine->set_buffer_size = jack_set_buffer_size;

	engine->next_client_id = 1;
	engine->timebase_client = 0;
	engine->port_max = 128;
	engine->rtpriority = rtpriority;
	engine->silent_buffer = 0;
	engine->getthehelloutathere = FALSE;

	pthread_mutex_init (&engine->graph_lock, 0);
	pthread_mutex_init (&engine->buffer_lock, 0);
	pthread_mutex_init (&engine->port_lock, 0);

	engine->clients = 0;

	engine->port_segments = 0;
	engine->port_buffer_freelist = 0;

	engine->pfd_size = 16;
	engine->pfd_max = 0;
	engine->pfd = (struct pollfd *) malloc (sizeof (struct pollfd) * engine->pfd_size);

	engine->fifo_size = 16;
	engine->fifo = (int *) malloc (sizeof (int) * engine->fifo_size);
	for (i = 0; i < engine->fifo_size; i++) {
		engine->fifo[i] = -1;
	}

	/* Build a linked list of known port types. We use a list so that 
	   we can easily manage other data types without messing with
	   reallocation of arrays, etc.
	*/

	engine->port_types = NULL;
	for (i = 0; builtin_port_types[i].type_name; i++) {
		engine->port_types = g_slist_append (engine->port_types, &builtin_port_types[i]);
	}

	engine->external_client_cnt = 0;

	srandom (time ((time_t *) 0));

	engine->control_key = random();
	control_size = sizeof (jack_control_t) + (sizeof (jack_port_shared_t) * engine->port_max);

	if ((engine->control_shm_id = shmget (engine->control_key, control_size, IPC_CREAT|0644)) < 0) {
		jack_error ("cannot create engine control shared memory segment (%s)", strerror (errno));
		return 0;
	}
	
	if ((addr = shmat (engine->control_shm_id, 0, 0)) == (void *) -1) {
		jack_error ("cannot attach control shared memory segment (%s)", strerror (errno));
		shmctl (engine->control_shm_id, IPC_RMID, 0);
		return 0;
	}

	on_exit (shm_destroy, (void *) engine->control_shm_id);

	engine->control = (jack_control_t *) addr;

	/* Mark all ports as available */

	for (i = 0; i < engine->port_max; i++) {
		engine->control->ports[i].in_use = 0;
		engine->control->ports[i].id = i;
	}

	/* allocate internal port structures so that we can keep
	   track of port connections.
	*/

	engine->internal_ports = (jack_port_internal_t *) malloc (sizeof (jack_port_internal_t) * engine->port_max);

	for (i = 0; i < engine->port_max; i++) {
		engine->internal_ports[i].connections = 0;
	}

	if (make_sockets (engine->fds) < 0) {
		jack_error ("cannot create server sockets");
		return 0;
	}

	engine->control->port_max = engine->port_max;
	engine->control->real_time = realtime;
	engine->control->client_priority = engine->rtpriority - 1;
 
	engine->control->sample_rate = 0;
	engine->control->buffer_size = 0;
	engine->control->frame_time = 0;

	sprintf (engine->fifo_prefix, "/tmp/jack_fifo_%d", getpid());

	jack_create_fifo (engine, 0);
	jack_start_server (engine);

	return engine;
}

static int
jack_become_real_time (pthread_t thread, int priority)

{
	struct sched_param rtparam;
	int x;

	memset (&rtparam, 0, sizeof (rtparam));
	rtparam.sched_priority = priority;

	if ((x = pthread_setschedparam (thread, SCHED_FIFO, &rtparam)) != 0) {
		jack_error ("cannot set thread to real-time priority (FIFO/%d) (%d: %s)", rtparam.sched_priority, x, strerror (errno));
	}

	if (mlockall (MCL_CURRENT | MCL_FUTURE) != 0) {
	    jack_error ("cannot lock down memory for RT thread (%s)", strerror (errno));
	}

	return 0;
}

void
cancel_cleanup1 (void *arg)

{
	jack_engine_t *engine = (jack_engine_t *) arg;
	printf ("audio thread cancelled or finished\n");
	engine->driver->audio_stop (engine->driver);
}

void
cancel_cleanup2 (int status, void *arg)

{
	jack_engine_t *engine = (jack_engine_t *) arg;
	engine->driver->audio_stop (engine->driver);
	engine->driver->finish (engine->driver);
}

static void *
jack_audio_thread (void *arg)

{
	jack_engine_t *engine = (jack_engine_t *) arg;
	jack_driver_t *driver = engine->driver;
//	unsigned long start, end;

	if (engine->control->real_time) {
		jack_become_real_time (pthread_self(), engine->rtpriority);
	}

	pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	on_exit (cancel_cleanup2, engine);

	if (driver->audio_start (driver)) {
		jack_error ("cannot start driver");
		pthread_exit (0);
	}

	while (1) {
//		start = end;
		if (driver->wait (driver)) {
			break;
		}
//		rdtscl (end);
//		printf ("driver cycle time: %.6f usecs\n", ((float) (end - start)) / 450.00f);
	}

	pthread_exit (0);
}

int
jack_run (jack_engine_t *engine)

{
	if (engine->driver == 0) {
		jack_error ("engine driver not set; cannot start");
		return -1;
	}
	return pthread_create (&engine->audio_thread, 0, jack_audio_thread, engine);
}
int
jack_wait (jack_engine_t *engine)

{
	void *ret = 0;
	int err;

	if ((err = pthread_join (engine->audio_thread, &ret)) != 0)  {
		switch (err) {
		case EINVAL:
			jack_error ("cannot join with audio thread (thread detached, or another thread is waiting)");
			break;
		case ESRCH:
			jack_error ("cannot join with audio thread (thread no longer exists)");
			break;
		case EDEADLK:
			jack_error ("programming error: jack_wait() called by audio thread");
			break;
		default:
			jack_error ("cannot join with audio thread (%s)", strerror (errno));
		}
	}
	return (int) ret;
}

int 
jack_engine_delete (jack_engine_t *engine)

{
	pthread_cancel (engine->audio_thread);
	return 0;
}

static jack_client_internal_t *
jack_client_internal_new (jack_engine_t *engine, int fd, jack_client_connect_request_t *req)

{
	jack_client_internal_t *client;
	key_t shm_key = 0;
	int shm_id = 0;
	void *addr = 0;

	switch (req->type) {
	case ClientDynamic:
	case ClientDriver:
		break;

	case ClientOutOfProcess:

		shm_key = random();
		
		if ((shm_id = shmget (shm_key, sizeof (jack_client_control_t), IPC_CREAT|0666)) < 0) {
			jack_error ("cannot create client control block");
			return 0;
		}

		if ((addr = shmat (shm_id, 0, 0)) == (void *) -1) {
			jack_error ("cannot attach new client control block");
			shmctl (shm_id, IPC_RMID, 0);
			return 0;
		}

		break;
	}

	client = (jack_client_internal_t *) malloc (sizeof (jack_client_internal_t));

	client->request_fd = fd;
	client->event_fd = -1;
	client->ports = 0;
	client->fed_by = 0;
	client->rank = UINT_MAX;
	client->next_client = NULL;
	client->handle = NULL;

	if (req->type != ClientOutOfProcess) {
		
		client->control = (jack_client_control_t *) malloc (sizeof (jack_client_control_t));		

	} else {

		client->shm_id = shm_id;
		client->shm_key = shm_key;
		client->control = (jack_client_control_t *) addr;
	}

	client->control->type = req->type;
	client->control->active = FALSE;
	client->control->dead = FALSE;
	client->control->id = engine->next_client_id++;
	strcpy ((char *) client->control->name, req->name);

	client->control->process = NULL;
	client->control->process_arg = NULL;
	client->control->bufsize = NULL;
	client->control->bufsize_arg = NULL;
	client->control->srate = NULL;
	client->control->srate_arg = NULL;
	client->control->port_register = NULL;
	client->control->port_register_arg = NULL;
	client->control->port_monitor = NULL;
	client->control->port_monitor_arg = NULL;

	if (req->type == ClientDynamic) {
		if (jack_load_client (engine, client, req->object_path)) {
			jack_error ("cannot dynamically load client from \"%s\"", req->object_path);
			jack_client_delete (engine, client);
			return 0;
		}
	}

	return client;
}

static void
jack_port_clear_connections (jack_engine_t *engine, jack_port_internal_t *port)
{
	GSList *node, *next;

	for (node = port->connections; node; ) {
		next = g_slist_next (node);
		jack_port_disconnect_internal (engine, 
						((jack_connection_internal_t *) node->data)->source,
						((jack_connection_internal_t *) node->data)->destination, 
						FALSE);
		node = next;
	}

	g_slist_free (port->connections);
	port->connections = 0;
}


static void
jack_remove_client (jack_engine_t *engine, jack_client_internal_t *client)
{
	GSList *node;
	int i;

	printf ("removing client %s\n", client->control->name);

	pthread_mutex_lock (&engine->graph_lock);
	
	client->control->dead = TRUE;

	if (client == engine->timebase_client) {
		engine->timebase_client = 0;
		engine->control->frame_time = 0;
	}

	jack_client_disconnect (engine, client);

	for (node = engine->clients; node; node = g_slist_next (node)) {
		if (((jack_client_internal_t *) node->data)->control->id == client->control->id) {
			engine->clients = g_slist_remove_link (engine->clients, node);
			g_slist_free_1 (node);
			break;
		}
	}

	jack_client_do_deactivate (engine, client);

	/* rearrange the pollfd array so that things work right the 
	   next time we go into poll(2).
	*/

	for (i = 0; i < engine->pfd_max; i++) {
		if (engine->pfd[i].fd == client->request_fd) {
			if (i+1 < engine->pfd_max) {
				memmove (&engine->pfd[i], &engine->pfd[i+1], sizeof (struct pollfd) * (engine->pfd_max - i));
			}
			engine->pfd_max--;
		}
	}

	close (client->event_fd);
	close (client->request_fd);

	jack_client_delete (engine, client);

	pthread_mutex_unlock (&engine->graph_lock);
}

static void
jack_client_delete (jack_engine_t *engine, jack_client_internal_t *client)

{
	jack_client_disconnect (engine, client);

	if (jack_client_is_inprocess (client)) {
		jack_client_unload (client);
		free ((char *) client->control);
	} else {
		shmdt ((void *) client->control);
	}

	free (client);
}

jack_client_internal_t *
jack_client_by_name (jack_engine_t *engine, const char *name)

{
	jack_client_internal_t *client = NULL;
	GSList *node;

	pthread_mutex_lock (&engine->graph_lock);

	for (node = engine->clients; node; node = g_slist_next (node)) {
		if (strcmp ((const char *) ((jack_client_internal_t *) node->data)->control->name, name) == 0) {
			client = (jack_client_internal_t *) node->data;
			break;
		}
	}

	pthread_mutex_unlock (&engine->graph_lock);
	return client;
}

jack_client_internal_t *
jack_client_internal_by_id (jack_engine_t *engine, jack_client_id_t id)

{
	jack_client_internal_t *client = NULL;
	GSList *node;

	/* call tree ***MUST HOLD*** engine->graph_lock */

	for (node = engine->clients; node; node = g_slist_next (node)) {
		if (((jack_client_internal_t *) node->data)->control->id == id) {
			client = (jack_client_internal_t *) node->data;
			break;
		}
	}

	return client;
}

static int
jack_deliver_event (jack_engine_t *engine, jack_client_internal_t *client, jack_event_t *event)
{
	char status;
	
	if (client->control->dead) {
		return 0;
	}

	if (jack_client_is_inprocess (client)) {

		switch (event->type) {
		case PortConnected:
		case PortDisconnected:
			jack_client_handle_port_connection (client->control->private_internal_client, event);
			break;

		case GraphReordered:
			jack_error ("reorder event delivered to internal client!");
			break;

		case BufferSizeChange:
			if (client->control->bufsize) {
				client->control->bufsize (event->x.n, client->control->bufsize_arg);
			}
			break;

		case SampleRateChange:
			if (client->control->srate) {
				client->control->srate (event->x.n, client->control->bufsize_arg);
			}
			break;

		case PortMonitor:
			if (client->control->port_monitor) {
				client->control->port_monitor (event->x.port_id, TRUE, client->control->port_monitor_arg);
			}
			break;

		case PortUnMonitor:
			if (client->control->port_monitor) {
				client->control->port_monitor (event->x.port_id, FALSE, client->control->port_monitor_arg);
			}
			break;

		default:
			/* internal clients don't need to know */
			break;
		}

	} else {
		if (write (client->event_fd, event, sizeof (*event)) != sizeof (*event)) {
			jack_error ("cannot send event to client [%s] (%s)", client->control->name, strerror (errno));
			return -1;
		}
		
		if (read (client->event_fd, &status, sizeof (status)) != sizeof (status)) {
			jack_error ("cannot read event response from client [%s] (%s)", client->control->name, strerror (errno));
			return -1;
		}
	}

	return 0;
}

int
jack_client_set_order (jack_engine_t *engine, jack_client_internal_t *client)

{
	jack_event_t event;

	event.type = GraphReordered;
	event.x.n = client->rank;

	return jack_deliver_event (engine, client, &event);
}

int
jack_rechain_graph (jack_engine_t *engine, int take_lock)

{
	GSList *node, *next;
	unsigned long n;
	int err = 0;
	int set;
	jack_client_internal_t *client, *subgraph_client, *next_client;

	if (take_lock) {
		pthread_mutex_lock (&engine->graph_lock);
	}

	/* We're going to try to avoid reconnecting clients that 
	   don't need to be reconnected. This is slightly tricky, 
	   but worth it for performance reasons.
	*/

	subgraph_client = 0;

	if ((node = engine->clients) == 0) {
		goto done;
	}

	client = (jack_client_internal_t *) node->data;
	if ((next = g_slist_next (node)) == NULL) {
		next_client = 0;
	} else {
		next_client = (jack_client_internal_t *) next->data;
	}
	n = 0;

	do {
		if (client->rank != n || client->next_client != next_client) {
			client->rank = n;
			client->next_client = next_client;
			set = TRUE;
		} else {
			set = FALSE;
		}

		if (jack_client_is_inprocess (client)) {

			/* break the chain for the current subgraph. the server
			   will wait for chain on the nth FIFO, and will
			   then execute this in-process client.
			*/

			if (subgraph_client) {
				subgraph_client->subgraph_wait_fd = jack_get_fifo_fd (engine, n);
			}

			subgraph_client = 0;
			
		} else {

			if (subgraph_client == 0) {

				/* start a new subgraph. the engine will start the chain
				   by writing to the nth FIFO.
				*/

				subgraph_client = client;
				subgraph_client->subgraph_start_fd = jack_get_fifo_fd (engine, n);
			} 

			if (set) {
				jack_client_set_order (engine, client);
			}
			
			n++;
		}

		if (next == 0) {
			break;
		}

		node = next;
		client = (jack_client_internal_t *) node->data;

		if ((next = g_slist_next (node)) == 0) {
			next_client = 0;
		} else {
			next_client = (jack_client_internal_t *) next->data;
		}

	} while (1);
	
	if (subgraph_client) {
		subgraph_client->subgraph_wait_fd = jack_get_fifo_fd (engine, n);
	}

  done:
	if (take_lock) {
		pthread_mutex_unlock (&engine->graph_lock);
	}

	return err;
}

static void
jack_trace_terminal (jack_client_internal_t *c1, jack_client_internal_t *rbase)
{
	jack_client_internal_t *c2;

	/* make a copy of the existing list of routes that feed c1 */

	GSList *existing;
	GSList *node;

	if (c1->fed_by == 0) {
		return;
	}

	existing = g_slist_copy (c1->fed_by);

	/* for each route that feeds c1, recurse, marking it as feeding
	   rbase as well.
	*/

	for (node = existing; node; node = g_slist_next  (node)) {

		c2 = (jack_client_internal_t *) node->data;

		/* c2 is a route that feeds c1 which somehow feeds base. mark
		   base as being fed by c2
		*/

		rbase->fed_by = g_slist_prepend (rbase->fed_by, c2);

		if (c2 != rbase && c2 != c1) {

			/* now recurse, so that we can mark base as being fed by
			   all routes that feed c2
			*/

			jack_trace_terminal (c2, rbase);
		}

	}
}

static int 
jack_client_sort (jack_client_internal_t *a, jack_client_internal_t *b)

{
	/* the driver client always comes after everything else */

	if (a->control->type == ClientDriver) {
		return 1;
	}

	if (b->control->type == ClientDriver) {
		return -1;
	}

	if (g_slist_find (a->fed_by, b)) {
		/* a comes after b */
		return 1;
	} else if (g_slist_find (b->fed_by, a)) {
		/* b comes after a */
		return -1;
	} else {
		/* we don't care */
		return 0;
	}
}

static int
jack_client_feeds (jack_client_internal_t *might, jack_client_internal_t *target)
{
	GSList *pnode, *cnode;

	/* Check every port of `might' for an outbound connection to `target'
	*/

	for (pnode = might->ports; pnode; pnode = g_slist_next (pnode)) {

		jack_port_internal_t *port;
		
		port = (jack_port_internal_t *) pnode->data;

		for (cnode = port->connections; cnode; cnode = g_slist_next (cnode)) {

			jack_connection_internal_t *c;

			c = (jack_connection_internal_t *) cnode->data;

			if (c->source->shared->client_id == might->control->id &&
			    c->destination->shared->client_id == target->control->id) {
				return 1;
			}
		}
	}
	
	return 0;
}

static void
jack_sort_graph (jack_engine_t *engine, int take_lock)
{
	GSList *node, *onode;
	jack_client_internal_t *client;
	jack_client_internal_t *oclient;

	if (take_lock) {
		pthread_mutex_lock (&engine->graph_lock);
	}

	for (node = engine->clients; node; node = g_slist_next (node)) {

		client = (jack_client_internal_t *) node->data;

		g_slist_free (client->fed_by);
		client->fed_by = 0;

		for (onode = engine->clients; onode; onode = g_slist_next (onode)) {
			
			oclient = (jack_client_internal_t *) onode->data;

			if (jack_client_feeds (oclient, client)) {
				client->fed_by = g_slist_prepend (client->fed_by, oclient);
			}
		}
	}

	for (node = engine->clients; node; node = g_slist_next (node)) {
		jack_trace_terminal ((jack_client_internal_t *) node->data,
				     (jack_client_internal_t *) node->data);
	}

	engine->clients = g_slist_sort (engine->clients, (GCompareFunc) jack_client_sort);
	jack_rechain_graph (engine, FALSE);

	if (take_lock) {
		pthread_mutex_unlock (&engine->graph_lock);
	}
}

static int 
jack_port_do_connect (jack_engine_t *engine,
		       const char *source_port,
		       const char *destination_port)
{
	jack_connection_internal_t *connection;
	jack_port_internal_t *srcport, *dstport;
	jack_port_id_t src_id, dst_id;

	fprintf (stderr, "trying to connect %s and %s\n", source_port, destination_port);

	if ((srcport = jack_get_port_by_name (engine, source_port)) == 0) {
		jack_error ("unknown source port in attempted connection [%s]", source_port);
		return -1;
	}

	if ((dstport = jack_get_port_by_name (engine, destination_port)) == 0) {
		jack_error ("unknown destination port in attempted connection [%s]", destination_port);
		return -1;
	}

	if ((dstport->shared->flags & JackPortIsInput) == 0) {
		jack_error ("destination port in attempted connection is not an input port");
		return -1;
	}

	if ((srcport->shared->flags & JackPortIsOutput) == 0) {
		jack_error ("source port in attempted connection is not an output port");
		return -1;
	}

	if (strcmp (srcport->shared->type_info.type_name,
		    dstport->shared->type_info.type_name) != 0) {
		jack_error ("ports used in attemped connection are not of the same data type");
		return -1;
	}

	connection = (jack_connection_internal_t *) malloc (sizeof (jack_connection_internal_t));

	connection->source = srcport;
	connection->destination = dstport;

	src_id = srcport->shared->id;
	dst_id = dstport->shared->id;

	pthread_mutex_lock (&engine->graph_lock);

	if (dstport->connections && dstport->shared->type_info.mixdown == NULL) {
		jack_error ("cannot make multiple connections to a port of type [%s]", dstport->shared->type_info.type_name);
		free (connection);
		return -1;
	} else {
		dstport->connections = g_slist_prepend (dstport->connections, connection);
		srcport->connections = g_slist_prepend (srcport->connections, connection);

		jack_sort_graph (engine, FALSE);
		
		jack_send_connection_notification (engine, srcport->shared->client_id, src_id, dst_id, TRUE);
		jack_send_connection_notification (engine, dstport->shared->client_id, dst_id, src_id, TRUE);
	}

	pthread_mutex_unlock (&engine->graph_lock);

	return 0;
}

int
jack_port_disconnect_internal (jack_engine_t *engine, 
				jack_port_internal_t *srcport, 
				jack_port_internal_t *dstport, 
				int sort_graph)

{
	GSList *node;
	jack_connection_internal_t *connect;
	int ret = -1;
	jack_port_id_t src_id, dst_id;

	/* call tree **** MUST HOLD **** engine->graph_lock. */
	
	printf ("disconnecting %s and %s\n", srcport->shared->name, dstport->shared->name);
			
	for (node = srcport->connections; node; node = g_slist_next (node)) {

		connect = (jack_connection_internal_t *) node->data;

		if (connect->source == srcport && connect->destination == dstport) {

			srcport->connections = g_slist_remove (srcport->connections, connect);
			dstport->connections = g_slist_remove (dstport->connections, connect);

			src_id = srcport->shared->id;
			dst_id = dstport->shared->id;

			jack_send_connection_notification (engine, srcport->shared->client_id, src_id, dst_id, FALSE);
			jack_send_connection_notification (engine, dstport->shared->client_id, dst_id, src_id, FALSE);

			free (connect);
			ret = 0;
			break;
		}
	}

	if (sort_graph) {
		jack_sort_graph (engine, FALSE);
	}

	if (ret == -1) {
		printf ("disconnect failed\n");
	}

	return ret;
}

static int 
jack_port_do_disconnect (jack_engine_t *engine,
			 const char *source_port,
			 const char *destination_port)
{
	jack_port_internal_t *srcport, *dstport;
	int ret = -1;

	if ((srcport = jack_get_port_by_name (engine, source_port)) == 0) {
		jack_error ("unknown source port in attempted connection [%s]", source_port);
		return -1;
	}

	if ((dstport = jack_get_port_by_name (engine, destination_port)) == 0) {
		jack_error ("unknown destination port in attempted connection [%s]", destination_port);
		return -1;
	}

	pthread_mutex_lock (&engine->graph_lock);

	ret = jack_port_disconnect_internal (engine, srcport, dstport, TRUE);

	pthread_mutex_unlock (&engine->graph_lock);
	return ret;
}

static int
jack_create_fifo (jack_engine_t *engine, int which_fifo)

{
	char path[FIFO_NAME_SIZE+1];

	sprintf (path, "%s-%d", engine->fifo_prefix, which_fifo);

	if (mknod (path, 0666|S_IFIFO, 0) < 0) {
		if (errno != EEXIST) {
			jack_error ("cannot create inter-client FIFO [%s] (%s)", path, strerror (errno));
			return -1;
		}

	} else {
		on_exit (unlink_path, strdup (path));
	}

	jack_get_fifo_fd (engine, which_fifo);

	return 0;
}

static int 
jack_get_fifo_fd (jack_engine_t *engine, int which_fifo)

{
	char path[FIFO_NAME_SIZE+1];

	sprintf (path, "%s-%d", engine->fifo_prefix, which_fifo);

	if (which_fifo >= engine->fifo_size) {
		int i;

		engine->fifo = (int *) realloc (engine->fifo, sizeof (int) * engine->fifo_size + 16);
		for (i = engine->fifo_size; i < engine->fifo_size + 16; i++) {
			engine->fifo[i] = -1;
		}
		engine->fifo_size += 16;
	}

	if (engine->fifo[which_fifo] < 0) {
		if ((engine->fifo[which_fifo] = open (path, O_RDWR|O_CREAT, 0666)) < 0) {
			jack_error ("cannot open fifo [%s] (%s)", path, strerror (errno));
			return -1;
		}
	}

	return engine->fifo[which_fifo];
}

int
jack_use_driver (jack_engine_t *engine, jack_driver_t *driver)

{
	if (engine->driver) {
		engine->driver->detach (engine->driver, engine);
		engine->driver = 0;
	}

	if (driver) {
		if (driver->attach (driver, engine)) {
			return -1;
		}
	}

	engine->driver = driver;
	return 0;
}

/* PORT RELATED FUNCTIONS */


jack_port_id_t
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
		return NoPort;
	}

	return i;
}

static void
jack_port_release (jack_engine_t *engine, jack_port_internal_t *port)

{
	/* XXX add the buffer used by the port back the (correct) freelist */

	pthread_mutex_lock (&engine->port_lock);
	port->shared->in_use = 0;
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
	GSList *node;
	jack_port_id_t port_id;
	jack_port_shared_t *shared;
	jack_port_internal_t *port;
	jack_client_internal_t *client;
	jack_port_type_info_t *type_info;

	pthread_mutex_lock (&engine->graph_lock);
	if ((client = jack_client_internal_by_id (engine, req->x.port_info.client_id)) == 0) {
		jack_error ("unknown client id in port registration request");
		return -1;
	}
	pthread_mutex_unlock (&engine->graph_lock);

	if ((port_id = jack_get_free_port (engine)) == NoPort) {
		jack_error ("no ports available!");
		return -1;
	}

	shared = &engine->control->ports[port_id];

	strcpy (shared->name, req->x.port_info.name);

	shared->client_id = req->x.port_info.client_id;
	shared->flags = req->x.port_info.flags;
	shared->locked = 0;
	shared->buffer_size = req->x.port_info.buffer_size;

	port = &engine->internal_ports[port_id];

	port->shared = shared;
	port->connections = 0;

	type_info = NULL;

	for (node = engine->port_types; node; node = g_slist_next (node)) {
		
		if (strcmp (req->x.port_info.type, ((jack_port_type_info_t *) node->data)->type_name) == 0) {
			type_info = (jack_port_type_info_t *) node->data;
			break;
		}
	}

	if (type_info == NULL) {

		/* not a builtin type, so allocate a new type_info structure,
		   and fill it appropriately.
		*/
		
		type_info = (jack_port_type_info_t *) malloc (sizeof (jack_port_type_info_t));

		type_info->type_name = strdup (req->x.port_info.type);
		type_info->mixdown = NULL;            /* we have no idea how to mix this */
		type_info->buffer_scale_factor = -1;  /* use specified port buffer size */

		engine->port_types = g_slist_prepend (engine->port_types, type_info);
	}


	memcpy (&port->shared->type_info, type_info, sizeof (jack_port_type_info_t));

	if (jack_port_assign_buffer (engine, port)) {
		jack_error ("cannot assign buffer for port");
		return -1;
	}

	pthread_mutex_lock (&engine->graph_lock);
	client->ports = g_slist_prepend (client->ports, port);
	jack_port_registration_notify (engine, port_id, TRUE);
	pthread_mutex_unlock (&engine->graph_lock);

	req->x.port_info.port_id = port_id;

	return 0;
}

int
jack_port_do_unregister (jack_engine_t *engine, jack_request_t *req)


{
	jack_client_internal_t *client;
	jack_port_shared_t *shared;
	jack_port_internal_t *port;

	if (req->x.port_info.port_id < 0 || req->x.port_info.port_id > engine->port_max) {
		jack_error ("invalid port ID %d in unregister request\n", req->x.port_info.port_id);
		return -1;
	}

	shared = &engine->control->ports[req->x.port_info.port_id];

	pthread_mutex_lock (&engine->graph_lock);
	if ((client = jack_client_internal_by_id (engine, shared->client_id)) == NULL) {
		jack_error ("unknown client id in port registration request");
		return -1;
	}
	pthread_mutex_unlock (&engine->graph_lock);

	port = &engine->internal_ports[req->x.port_info.port_id];

	jack_port_release (engine, &engine->internal_ports[req->x.port_info.port_id]);
	
	pthread_mutex_lock (&engine->graph_lock);
	client->ports = g_slist_remove (client->ports, port);
	jack_port_registration_notify (engine, req->x.port_info.port_id, FALSE);
	pthread_mutex_unlock (&engine->graph_lock);

	return 0;
}

void
jack_port_registration_notify (jack_engine_t *engine, jack_port_id_t port_id, int yn)

{
	jack_event_t event;
	jack_client_internal_t *client;
	GSList *node;

	event.type = (yn ? PortRegistered : PortUnregistered);
	event.x.port_id = port_id;
	
	for (node = engine->clients; node; node = g_slist_next (node)) {
		
		client = (jack_client_internal_t *) node->data;

		if (!client->control->active) {
			continue;
		}

		if (client->control->port_register) {
			if (jack_deliver_event (engine, client, &event)) {
				jack_error ("cannot send port registration notification to %s (%s)",
					     client->control->name, strerror (errno));
			}
		}
	}
}

int
jack_port_assign_buffer (jack_engine_t *engine, jack_port_internal_t *port)
{
	GSList *node;
	jack_port_segment_info_t *psi;
	jack_port_buffer_info_t *bi;

	port->shared->shm_key = -1;

	if (port->shared->flags & JackPortIsInput) {
		return 0;
	}
	
	pthread_mutex_lock (&engine->buffer_lock);

	if (engine->port_buffer_freelist == NULL) {
		jack_error ("no more buffers available!");
		goto out;
	}

	bi = (jack_port_buffer_info_t *) engine->port_buffer_freelist->data;
	
	for (node = engine->port_segments; node; node = g_slist_next (node)) {

		psi = (jack_port_segment_info_t *) node->data;

		if (bi->shm_key == psi->shm_key) {
			port->shared->shm_key = psi->shm_key;
			port->shared->offset = bi->offset;
			break;
		}
	}
	
	if (port->shared->shm_key >= 0) {
		engine->port_buffer_freelist = g_slist_remove (engine->port_buffer_freelist, bi);
	} else {
		jack_error ("port segment info for 0x%x:%d not found!", bi->shm_key, bi->offset);
	}

  out:
	pthread_mutex_unlock (&engine->buffer_lock);

	if (port->shared->shm_key < 0) {
		return -1;
	} else {
		return 0;
	}
}

static jack_port_internal_t *
jack_get_port_by_name (jack_engine_t *engine, const char *name)

{
	jack_port_id_t id;

	/* Note the potential race on "in_use". Other design
	   elements prevent this from being a problem.
	*/

	for (id = 0; id < engine->port_max; id++) {
		if (engine->control->ports[id].in_use && strcmp (engine->control->ports[id].name, name) == 0) {
			return &engine->internal_ports[id];
		}
	}

	return NULL;
}

static int
jack_send_connection_notification (jack_engine_t *engine, jack_client_id_t client_id, 
				    jack_port_id_t self_id, jack_port_id_t other_id, int connected)

{
	jack_client_internal_t *client;
	jack_event_t event;

	if ((client = jack_client_internal_by_id (engine, client_id)) == 0) {
		jack_error ("no such client %d during connection notification", client_id);
		return -1;
	}

	event.type = (connected ? PortConnected : PortDisconnected);
	event.x.self_id = self_id;
	event.y.other_id = other_id;

	if (jack_deliver_event (engine, client, &event)) {
		jack_error ("cannot send port connection notification to client %s (%s)", 
			     client->control->name, strerror (errno));
		return -1;
	}

	return 0;
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
