/* -*- mode: c; c-file-style: "bsd"; -*- */
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

#include <config.h>

#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <regex.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#ifdef USE_MLOCK
#include <sys/mman.h>
#endif /* USE_MLOCK */

#include <jack/internal.h>
#include <jack/jack.h>
#include <jack/engine.h>
#include <jack/pool.h>
#include <jack/jslist.h>
#include <jack/version.h>
#include <jack/shm.h>

#include <sysdeps/time.h>
JACK_TIME_GLOBAL_DECL;			/* One instance per process. */

#include "local.h"

#include <sysdeps/poll.h>
#include <sysdeps/ipc.h>

#ifdef JACK_USE_MACH_THREADS
#include <sysdeps/pThreadUtilities.h>
#endif

#ifdef WITH_TIMESTAMPS
#include <jack/timestamps.h>
#endif /* WITH_TIMESTAMPS */

char *jack_server_dir = DEFAULT_TMP_DIR;

void
jack_set_server_dir (const char *path)
{
	jack_error ("jack_set_server_dir() is deprecated.\n"
		    "Please contact the program's author");
	jack_server_dir = strdup (path);
}

static pthread_mutex_t client_lock;
static pthread_cond_t  client_ready;
void *jack_zero_filled_buffer = NULL;

#define event_fd pollfd[0].fd
#define graph_wait_fd pollfd[1].fd

typedef struct {
    int status;
    struct _jack_client *client;
    const char *client_name;
} client_info;

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

void 
default_jack_error_callback (const char *desc)
{
	fprintf(stderr, "%s\n", desc);
}

void 
silent_jack_error_callback (const char *desc)
{
}

void (*jack_error_callback)(const char *desc) = &default_jack_error_callback;

static int
oop_client_deliver_request (void *ptr, jack_request_t *req)
{
	int wok, rok;
	jack_client_t *client = (jack_client_t*) ptr;

	wok = (write (client->request_fd, req, sizeof (*req))
	       == sizeof (*req));
	rok = (read (client->request_fd, req, sizeof (*req))
	       == sizeof (*req));

	if (wok && rok)			/* everything OK? */
		return req->status;

	req->status = -1;		/* request failed */

	/* check for server shutdown */
	if (client->engine->engine_ok == 0)
		return req->status;

	/* otherwise report errors */
	if (!wok)
		jack_error ("cannot send request type %d to server",
				    req->type);
	if (!rok)
		jack_error ("cannot read result for request type %d from"
			    " server (%s)", req->type, strerror (errno));
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
	client->ports = NULL;
	client->engine = NULL;
	client->control = NULL;
	client->thread_ok = FALSE;
#if JACK_USE_MACH_THREADS 
	client->rt_thread_ok = FALSE;
#endif
	client->first_active = TRUE;
	client->on_shutdown = NULL;
	client->n_port_types = 0;
	client->port_segment = NULL;

	return client;
}

/*
 * Build the jack_client_t structure for an internal client.
 */
jack_client_t *
jack_client_alloc_internal (jack_client_control_t *cc, jack_engine_t* engine)
{
	jack_client_t* client;

	client = jack_client_alloc ();

	client->control = cc;
	client->engine = engine->control;
	
	client->n_port_types = client->engine->n_port_types;
	client->port_segment = &engine->port_segment[0];

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

void
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
			if (port->mix_buffer) {
				jack_pool_release (port->mix_buffer);
				port->mix_buffer = NULL;
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

	sprintf (path, "%s-%" PRIu32, client->fifo_prefix, event->x.n);

	if ((client->graph_wait_fd = open (path, O_RDONLY|O_NONBLOCK)) < 0) {
		jack_error ("cannot open specified fifo [%s] for reading (%s)", path, strerror (errno));
		return -1;
	}


	DEBUG ("opened new graph_wait_fd %d (%s)", client->graph_wait_fd, path);

	sprintf (path, "%s-%" PRIu32, client->fifo_prefix, event->x.n+1);
	
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
		jack_error ("cannot create client socket (%s)",
			    strerror (errno));
		return -1;
	}

	addr.sun_family = AF_UNIX;
	snprintf (addr.sun_path, sizeof (addr.sun_path) - 1, "%s/jack_%d",
		  jack_server_dir, which);

	if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) < 0) {
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
		jack_error ("cannot create client event socket (%s)",
			    strerror (errno));
		return -1;
	}

	addr.sun_family = AF_UNIX;
	snprintf (addr.sun_path, sizeof (addr.sun_path) - 1, "%s/jack_ack_0",
		  jack_server_dir);

	if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) < 0) {
		jack_error ("cannot connect to jack server for events",
			    strerror (errno));
		close (fd);
		return -1;
	}

	req.client_id = client->control->id;

	if (write (fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot write event connect request to server (%s)",
			    strerror (errno));
		close (fd);
		return -1;
	}

	if (read (fd, &res, sizeof (res)) != sizeof (res)) {
		jack_error ("cannot read event connect result from server (%s)",
			    strerror (errno));
		close (fd);
		return -1;
	}

	if (res.status != 0) {
		jack_error ("cannot connect to server for event stream (%s)",
			    strerror (errno));
		close (fd);
		return -1;
	}

	return fd;
}

static void
_start_server (void)
{
	FILE* fp = 0;
	char filename[255];
	char arguments[255];
	char buffer[255];
	char* command = 0;
	size_t pos = 0;
	size_t result = 0;
	char** argv = 0;
	int i = 0;
	int good = 0;
	int ret;


	snprintf(filename, 255, "%s/.jackdrc", getenv("HOME"));
	fp = fopen(filename, "r");

	if (!fp) {
		fp = fopen("/etc/jackd.conf", "r");
	}

	if (fp) {
		arguments[0] = '\0';
		ret = fscanf(fp, "%s", buffer);
		while(ret != 0 && ret != EOF) {
			strcat(arguments, buffer);
			strcat(arguments, " ");
			ret = fscanf(fp, "%s", buffer);
		}
		if (strlen(arguments) > 0) {
			good = 1;
		}
	}

	if (!good) {

#if defined(USE_CAPABILITIES)
		command = JACK_LOCATION "/jackstart";
		strncpy(arguments, JACK_LOCATION "/jackstart -T -R -d"
			JACK_DEFAULT_DRIVER " -p 512", 255);
#else /* !USE_CAPABILITIES */
		command = JACK_LOCATION "/jackd";
		strncpy(arguments, JACK_LOCATION "/jackd -T -d"
			JACK_DEFAULT_DRIVER, 255);
#endif /* USE_CAPABILITIES */

	} else {
		result = strcspn(arguments, " ");
		command = (char*)malloc(result+1);
		strncpy(command, arguments, result);
		command[result] = '\0';
	}

	argv = (char**)malloc(255);
	while(1) {
		result = strcspn(arguments+pos, " ");
		if (result == 0) {
			break;
		}
		argv[i] = (char*)malloc(result+1);

		strncpy(argv[i], arguments+pos, result);
		argv[i][result] = '\0';

		pos += result+1;
		++i;
	}
	argv[i] = 0;

	execv (command, argv);

	/* If execv() succeeds, it does not return.  There's no point
	 * in calling jack_error() here in the child process. */
	perror ("exec of JACK server failed");
}

int
start_server (void)
{
	pid_t pid1, pid2;
	
	if (getenv("JACK_NO_START_SERVER")) {
		return 1;
	}

	switch (pid1 = fork()) {
		case -1:
			return 1;

		case 0:
			switch (pid2 = fork()) {
				case -1:
					return 1;
				case 0:
					_start_server();
			}
    		_exit(0);
	}
	return 0;
}

static int
jack_request_client (ClientType type, const char* client_name,
		     const char* so_name, const char* so_data,
		     jack_client_connect_result_t *res, int *req_fd)
{
	jack_client_connect_request_t req;

	*req_fd = -1;

	memset (&req, 0, sizeof (req));

	if (strlen (client_name) >= sizeof (req.name)) {
		jack_error ("\"%s\" is too long to be used as a JACK client"
			    " name.\n"
			    "Please use %lu characters or less.",
			    client_name, sizeof (req.name));
		return -1;
	}

	if (strlen (so_name) > sizeof (req.object_path) - 1) {
		jack_error ("\"%s\" is too long to be used as a JACK shared"
			    " object name.\n"
			     "Please use %lu characters or less.",
			    so_name, sizeof (req.object_path) - 1);
		return -1;
	}

	if (strlen (so_data) > sizeof (req.object_data) - 1) {
		jack_error ("\"%s\" is too long to be used as a JACK shared"
			    " object data string.\n"
			     "Please use %lu characters or less.",
			    so_data, sizeof (req.object_data) - 1);
		return -1;
	}

	if ((*req_fd = server_connect (0)) < 0) {
		if (start_server() == 0) {
			sleep(2);
			if ((*req_fd = server_connect (0)) < 0) {
				goto fail;
			}
		} else {
			goto fail;
		}
	}

	req.load = TRUE;
	req.type = type;
	snprintf (req.name, sizeof (req.name), "%s", client_name);
	snprintf (req.object_path, sizeof (req.object_path), "%s", so_name);
	snprintf (req.object_data, sizeof (req.object_data), "%s", so_data);

	if (write (*req_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot send request to jack server (%s)",
			    strerror (errno));
		goto fail;
	}

	if (read (*req_fd, res, sizeof (*res)) != sizeof (*res)) {

		if (errno == 0) {
			/* server shut the socket */
			jack_error ("could not attach as client "
				    "(duplicate client name?)");
			goto fail;
		}

		jack_error ("cannot read response from jack server (%s)",
			    strerror (errno));
		goto fail;
	}

	if (res->status) {
		jack_error ("could not attach as client "
			    "(duplicate client name?)");
		goto fail;
	}

	if (res->protocol_v != jack_protocol_version){
		jack_error ("application linked against incompatible libjack"
			    " version.");
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

int
jack_attach_port_segment (jack_client_t *client, jack_port_type_id_t ptid)
{
	/* Lookup, attach and register the port/buffer segments in use
	 * right now. 
	 */

	if (client->control->type != ClientExternal) {
		jack_error("Only external clients need attach port segments");
		abort();
	}

	/* make sure we have space to store the port
	   segment information.
	*/

	if (ptid >= client->n_port_types) {
		
		client->port_segment = (jack_shm_info_t*)
			realloc (client->port_segment,
				 sizeof (jack_shm_info_t) * (ptid+1));

		memset (&client->port_segment[client->n_port_types],
			0,
			sizeof (jack_shm_info_t) * 
			(ptid - client->n_port_types));
		
		client->n_port_types = ptid + 1;

	} else {

		/* release any previous segment */
		
#if JACK_USE_MACH_THREADS 
		/* Stephane Letz : letz@grame.fr
		Need a fix : this crash on MacOSX : temporary removed 
		jack_release_shm (&client->port_segment[ptid]);
		*/
#else
		jack_release_shm (&client->port_segment[ptid]);
#endif
	}

	/* get the index into the shm registry */

	client->port_segment[ptid].index =
		client->engine->port_types[ptid].shm_registry_index;

	/* attach the relevant segment */

	if (jack_attach_shm (&client->port_segment[ptid])) {
		jack_error ("cannot attach port segment shared memory"
			    " (%s)", strerror (errno));
		return -1;
	}

	/* The first chunk of the audio port segment will be set by
	 * the engine to be a zero-filled buffer.  This hasn't been
	 * done yet, but it will happen before the process cycle
	 * (re)starts. 
	 */

	if (ptid == JACK_AUDIO_PORT_TYPE) {
		jack_zero_filled_buffer =
			jack_shm_addr (&client->port_segment[ptid]);
	}

	return 0;
}

jack_client_t *
jack_client_new (const char *client_name)
{
	int req_fd = -1;
	int ev_fd = -1;
	jack_client_connect_result_t  res;
	jack_client_t *client;
	jack_port_type_id_t ptid;

	/* external clients need this initialized; internal clients
	   will use the setup in the server's address space.
	*/
	jack_init_time ();

	if (jack_initialize_shm ()) {
		jack_error ("Unable to initialize shared memory.");
		return NULL;
	}

	if (jack_request_client (ClientExternal, client_name, "", "",
				 &res, &req_fd)) {
		return NULL;
	}

	client = jack_client_alloc ();

	strcpy (client->fifo_prefix, res.fifo_prefix);
	client->request_fd = req_fd;

	client->pollfd[0].events = POLLIN|POLLERR|POLLHUP|POLLNVAL;
	client->pollfd[1].events = POLLIN|POLLERR|POLLHUP|POLLNVAL;

	/* attach the engine control/info block */
	client->engine_shm = res.engine_shm;
	if (jack_attach_shm (&client->engine_shm)) {
		jack_error ("cannot attached engine control shared memory"
			    " segment");
		goto fail;
	}
	
	client->engine = (jack_control_t *) jack_shm_addr (&client->engine_shm);

	/* now attach the client control block */
	client->control_shm = res.client_shm;
	if (jack_attach_shm (&client->control_shm)) {
		jack_error ("cannot attached client control shared memory"
			    " segment");
		goto fail;
	}
	
	client->control = (jack_client_control_t *)
		jack_shm_addr (&client->control_shm);

	/* nobody else needs to access this shared memory any more, so
	   destroy it. because we have our own attachment to it, it won't
	   vanish till we exit (and release it).
	*/
	jack_destroy_shm (&client->control_shm);

	client->n_port_types = client->engine->n_port_types;
	client->port_segment = (jack_shm_info_t *)
		malloc (sizeof (jack_shm_info_t) * client->n_port_types);
	
	for (ptid = 0; ptid < client->n_port_types; ++ptid) {
		client->port_segment[ptid].index =
			client->engine->port_types[ptid].shm_registry_index;
		jack_attach_port_segment (client, ptid);
	}

	/* set up the client so that it does the right thing for an
	 * external client 
	 */
	client->control->deliver_request = oop_client_deliver_request;
	client->control->deliver_arg = client;

	if ((ev_fd = server_event_connect (client)) < 0) {
		goto fail;
	}

	client->event_fd = ev_fd;
        
#ifdef JACK_USE_MACH_THREADS
        /* specific resources for server/client real-time thread
	 * communication */
	client->clienttask = mach_task_self();
        
	if (task_get_bootstrap_port(client->clienttask, &client->bp)){
            jack_error ("Can't find bootstrap port");
            goto fail;
        }
        
        if (allocate_mach_clientport(client, res.portnum) < 0) {
            jack_error("Can't allocate mach port");
            goto fail; 
        }; 
#endif /* JACK_USE_MACH_THREADS */
 	return client;
	
  fail:
	if (client->engine) {
		jack_release_shm (&client->engine_shm);
		client->engine = 0;
	}
	if (client->control) {
		jack_release_shm (&client->control_shm);
		client->control = 0;
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
		return;
	}

	if (write (fd, &req, sizeof (req)) != sizeof(req)) {
		jack_error ("cannot deliver ClientUnload request to JACK server.");
	}
	
	/* no response to this request */
	
	close (fd);
	return;
}

#if JACK_USE_MACH_THREADS 

int
jack_drop_real_time_scheduling (pthread_t thread)
{
	setThreadToPriority(thread, 31, false, 10000000);
	return 0;       
}

int
jack_acquire_real_time_scheduling (pthread_t thread, int priority) //priority is unused
{
	setThreadToPriority(thread, 96, true, 10000000);
	return 0;
}

#else

int
jack_drop_real_time_scheduling (pthread_t thread)
{
	struct sched_param rtparam;
	int x;
	
	memset (&rtparam, 0, sizeof (rtparam));
	rtparam.sched_priority = 0;
	
	if ((x = pthread_setschedparam (thread, SCHED_OTHER, &rtparam)) != 0) {
		jack_error ("cannot switch to normal scheduling priority(%s)\n", strerror (errno));
		return -1;
	}
        return 0;
}

int
jack_acquire_real_time_scheduling (pthread_t thread, int priority)
{
	struct sched_param rtparam;
	int x;
	
	memset (&rtparam, 0, sizeof (rtparam));
	rtparam.sched_priority = priority;
	
	if ((x = pthread_setschedparam (thread, SCHED_FIFO, &rtparam)) != 0) {
		jack_error ("cannot use real-time scheduling (FIFO/%d) "
			    "(%d: %s)", rtparam.sched_priority, x,
			    strerror (x));
		return -1;
	}
        return 0;
}

#endif

int
jack_set_freewheel (jack_client_t* client, int onoff)
{
	jack_request_t request;
	request.type = onoff ? FreeWheel : StopFreeWheel;
	return jack_client_deliver_request (client, &request);
}

void
jack_start_freewheel (jack_client_t* client)
{
	jack_client_control_t *control = client->control;

	if (client->engine->real_time) {
#if JACK_USE_MACH_THREADS 
		jack_drop_real_time_scheduling (client->process_thread);
#else
		jack_drop_real_time_scheduling (client->thread);
#endif
	}

	if (control->freewheel_cb) {
		control->freewheel_cb (1, control->freewheel_arg);
	}
}

void
jack_stop_freewheel (jack_client_t* client)
{
	jack_client_control_t *control = client->control;

	if (control->freewheel_cb) {
		control->freewheel_cb (0, control->freewheel_arg);
	}

	if (client->engine->real_time) {
#if JACK_USE_MACH_THREADS 
		jack_acquire_real_time_scheduling (client->process_thread,
						   client->engine->client_priority);
#else
		jack_acquire_real_time_scheduling (client->thread,
						   client->engine->client_priority);
#endif
	}
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
	client->control->pgrp = getpgrp();

	DEBUG ("client thread is now running");

	if (client->control->thread_init) {
		DEBUG ("calling client thread init callback");
		client->control->thread_init (client->control->thread_init_arg);
	}

	while (err == 0) {

	        if (client->engine->engine_ok == 0) {
		     if (client->on_shutdown)
			     client->on_shutdown (client->on_shutdown_arg);
		     else
			     jack_error ("engine unexpectedly shutdown; "
					 "thread exiting\n");
		     pthread_exit (0);
		}

		DEBUG ("client polling on event_fd and graph_wait_fd...");
                
		if (poll (client->pollfd, client->pollmax, 1000) < 0) {
			if (errno == EINTR) {
				continue;
			}
			jack_error ("poll failed in client (%s)",
				    strerror (errno));
			status = -1;
			break;
		}
                
		/* get an accurate timestamp on waking from poll for a
		 * process() cycle. 
		 */

		if (client->pollfd[1].revents & POLLIN) {
			control->awake_at = jack_get_microseconds();
		}

		DEBUG ("pfd[0].revents = 0x%x pfd[1].revents = 0x%x",
		       client->pollfd[0].revents,
		       client->pollfd[1].revents);

		pthread_testcancel();

		if ((client->pollfd[0].revents & ~POLLIN) ||
		    client->control->dead) {
			goto zombie;
		}

		if (client->pollfd[0].revents & POLLIN) {

			DEBUG ("client receives an event, "
			       "now reading on event fd");
                
			/* server has sent us an event. process the
			 * event and reply */

			if (read (client->event_fd, &event, sizeof (event))
			    != sizeof (event)) {
				jack_error ("cannot read server event (%s)",
					    strerror (errno));
				err++;
				break;
			}

			status = 0;

			switch (event.type) {
			case PortRegistered:
				if (control->port_register) {
					control->port_register
						(event.x.port_id, TRUE,
						 control->port_register_arg);
				} 
				break;

			case PortUnregistered:
				if (control->port_register) {
					control->port_register
						(event.x.port_id, FALSE,
						 control->port_register_arg);
				}
				break;

			case GraphReordered:
				status = jack_handle_reorder (client, &event);
				break;

			case PortConnected:
			case PortDisconnected:
				status = jack_client_handle_port_connection
					(client, &event);
				break;

			case BufferSizeChange:
				jack_client_invalidate_port_buffers (client);

				if (control->bufsize) {
					status = control->bufsize
						(control->nframes,
						 control->bufsize_arg);
				} 
				break;

			case SampleRateChange:
				if (control->srate) {
					status = control->srate
						(control->nframes,
						 control->srate_arg);
				}
				break;

			case XRun:
				if (control->xrun) {
					status = control->xrun
						(control->xrun_arg);
				}
				break;

			case AttachPortSegment:
				jack_attach_port_segment (client, event.y.ptid);
				break;
				
			case StartFreewheel:
				jack_start_freewheel (client);
				break;

			case StopFreewheel:
				jack_stop_freewheel (client);
				break;
			}

			DEBUG ("client has dealt with the event, writing "
			       "response on event fd");

			if (write (client->event_fd, &status, sizeof (status))
			    != sizeof (status)) {
				jack_error ("cannot send event response to "
					    "engine (%s)", strerror (errno));
				err++;
				break;
			}
		}

		if (client->pollfd[1].revents & ~POLLIN) {
			goto zombie;
		}

		if (client->pollfd[1].revents & POLLIN) {

#ifdef WITH_TIMESTAMPS
			jack_reset_timestamps ();
#endif

			DEBUG ("client %d signalled at %" PRIu64
			       ", awake for process at %" PRIu64
			       " (delay = %" PRIu64
			       " usecs) (wakeup on graph_wait_fd==%d)", 
			       getpid(),
			       control->signalled_at, 
			       control->awake_at, 
			       control->awake_at - control->signalled_at,
			       client->pollfd[1].fd);

			control->state = Running;

			if (control->sync_cb)
				jack_call_sync_client (client);

			if (control->process) {
				if (control->process (control->nframes,
						      control->process_arg)
				    == 0) {
					control->state = Finished;
				}
			} else {
				control->state = Finished;
			}

			if (control->timebase_cb)
				jack_call_timebase_master (client);

			control->finished_at = jack_get_microseconds();

#ifdef WITH_TIMESTAMPS
			jack_timestamp ("finished");
#endif
			/* pass the execution token along */

			DEBUG ("client finished processing at %" PRIu64
			       " (elapsed = %" PRIu64
			       " usecs), writing on graph_next_fd==%d", 
			       control->finished_at, 
			       control->finished_at - control->awake_at,
			       client->graph_next_fd);

			if (write (client->graph_next_fd, &c, sizeof (c))
			    != sizeof (c)) {
				jack_error ("cannot continue execution of the "
					    "processing graph (%s)",
					    strerror(errno));
				err++;
				break;
			}

			DEBUG ("client sent message to next stage by %" PRIu64
			       ", client reading on graph_wait_fd==%d", 
			       jack_get_microseconds(), client->graph_wait_fd);

#ifdef WITH_TIMESTAMPS
			jack_timestamp ("read pending byte from wait");
#endif
			DEBUG("reading cleanup byte from pipe\n");

			if ((read (client->graph_wait_fd, &c, sizeof (c))
			     != sizeof (c))) {
				DEBUG ("WARNING: READ FAILED!");
#if 0
				jack_error ("cannot complete execution of the "
				            "processing graph (%s)",
					    strerror(errno));
				err++;
				break;
#endif
			}

			/* check if we were killed during the process
			 * cycle (or whatever) */
			if (client->control->dead) {
				goto zombie;
			}

			DEBUG("process cycle fully complete\n");

#ifdef WITH_TIMESTAMPS
			jack_timestamp ("read done");
			jack_dump_timestamps (stdout);
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
		/* Need a fix : possibly make client crash if
		 * zombified without shutdown handler 
		 */
	}

	pthread_exit (0);
	/*NOTREACHED*/
	return 0;
}


#ifdef JACK_USE_MACH_THREADS
/* real-time thread : separated from the normal client thread, it will
 * communicate with the server using fast mach RPC mechanism */

static void *
jack_client_process_thread (void *arg)
{
	jack_client_t *client = (jack_client_t *) arg;
	jack_client_control_t *control = client->control;
	int err = 0;
      
	client->control->pid = getpid();
	DEBUG ("client process thread is now running");
        
	pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
        
	client->rt_thread_ok = TRUE;
            
   	while (err == 0) {
        
                if (jack_client_suspend(client) < 0) {
                        jack_error ("jack_client_process_thread : resume error");
                        goto zombie;
                }
                
                control->awake_at = jack_get_microseconds();
                
                DEBUG ("client resumed");
                 
                control->state = Running;

		if (control->sync_cb)
			jack_call_sync_client (client);

                if (control->process) {
                        if (control->process (control->nframes,
					      control->process_arg) == 0) {
                                control->state = Finished;
                        }
                } else {
                        control->state = Finished;
                }

		if (control->timebase_cb)
			jack_call_timebase_master (client);
                
                control->finished_at = jack_get_microseconds();
                
#ifdef WITH_TIMESTAMPS
                jack_timestamp ("finished");
#endif
                DEBUG ("client finished processing at %Lu (elapsed = %f usecs)", 
                               control->finished_at, ((float)(control->finished_at - control->awake_at)));
                  
                /* check if we were killed during the process cycle (or whatever) */

                if (client->control->dead) {
                        jack_error ("jack_client_process_thread : client->control->dead");
                        goto zombie;
                }

                DEBUG("process cycle fully complete\n");

        }
        
 	return (void *) ((intptr_t)err);

  zombie:
        
        jack_error ("jack_client_process_thread : zombified");
        
        client->rt_thread_ok = FALSE;

	if (client->on_shutdown) {
		jack_error ("zombified - calling shutdown handler");
		client->on_shutdown (client->on_shutdown_arg);
	} else {
		jack_error ("zombified - exiting from JACK");
		jack_client_close (client); /* Need a fix : possibly make client crash if zombified without shutdown handler */
	}

	pthread_exit (0);
	/*NOTREACHED*/
	return 0;
}
#endif /* JACK_USE_MACH_THREADS */

static int
jack_start_thread (jack_client_t *client)

{
#ifndef JACK_USE_MACH_THREADS
	int policy = SCHED_OTHER;
	struct sched_param client_param, temp_param;
#endif
	pthread_attr_t *attributes = 0;

	if (client->engine->real_time) {
		
		/* Get the client thread to run as an RT-FIFO
		   scheduled thread of appropriate priority.
		*/

		struct sched_param rt_param;

		attributes = (pthread_attr_t *)
			malloc (sizeof (pthread_attr_t));

		pthread_attr_init (attributes);

		if (pthread_attr_setschedpolicy (attributes, SCHED_FIFO)) {
			jack_error ("cannot set FIFO scheduling class for RT "
				    "thread");
			return -1;
		}

		if (pthread_attr_setscope (attributes, PTHREAD_SCOPE_SYSTEM)) {
			jack_error ("Cannot set scheduling scope for RT "
				    "thread");
			return -1;
		}

		memset (&rt_param, 0, sizeof (rt_param));
		rt_param.sched_priority = client->engine->client_priority;

		if (pthread_attr_setschedparam (attributes, &rt_param)) {
			jack_error ("Cannot set scheduling priority for RT "
				    "thread (%s)", strerror (errno));
			return -1;
		}
                
#ifdef USE_MLOCK
                if (client->engine->do_mlock
		    && (mlockall (MCL_CURRENT | MCL_FUTURE) != 0)) {
                    jack_error ("cannot lock down memory for RT thread (%s)",
				strerror (errno));
#ifdef ENSURE_MLOCK
                    return -1;
#endif /* ENSURE_MLOCK */
                }
#endif /* USE_MLOCK */
	}

#ifdef JACK_USE_MACH_THREADS

	if (pthread_create (&client->thread, attributes,
			    jack_client_thread, client)) {
		return -1;
	}

        /* a special real-time thread to call the "process"
	 * callback. It will communicate with the server using fast
	 * mach RPC mechanism */
        if (pthread_create (&client->process_thread, attributes,
			    jack_client_process_thread, client)) {
                jack_error("pthread_create failed for process_thread \n");
                return -1;
        }
        if (client->engine->real_time){
            /* time constraint thread */
            setThreadToPriority(client->process_thread, 96, TRUE, 10000000);
        }else{
            /* fixed priority thread */
            setThreadToPriority(client->process_thread, 63, TRUE, 10000000);
        }

#else /* !JACK_USE_MACH_THREADS */

	if (pthread_create (&client->thread, attributes,
			    jack_client_thread, client) == 0) {
		return 0;
	}

	if (!client->engine->real_time) {
		return -1;
	}

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
		jack_error ("Cannot get current client scheduler: %s",
			    strerror(errno));
		return -1;
	}
	memset (&client_param, 0, sizeof (client_param));
	if (sched_getparam (0, &client_param)) {
		jack_error ("Cannot get current client scheduler "
			    "parameters: %s", strerror(errno));
		return -1;
	}

	/* temporarily change the client process to SCHED_FIFO so that
	   the realtime thread can inherit the scheduler and priority
	*/
	memset (&temp_param, 0, sizeof (temp_param));
	temp_param.sched_priority = client->engine->client_priority;
	if (sched_setscheduler(0, SCHED_FIFO, &temp_param)) {
		jack_error ("Cannot temporarily set client to RT scheduler:"
			    " %s", strerror(errno));
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
		jack_error ("Cannot set scheduler inherit policy for RT "
			    "thread");
		return -1;
	}

	/* create the RT thread */
	if (pthread_create (&client->thread, attributes,
			    jack_client_thread, client)) {
		sched_setscheduler (0, policy, &client_param);
		return -1;
	}

	/* return the client process to the scheduler it was in before */
	if (sched_setscheduler (0, policy, &client_param)) {
		jack_error ("Cannot reset original client scheduler: %s",
			    strerror(errno));
		return -1;
	}

	/* check again... inheritance of policy and priority works in
	   jack_simple_client but not in ardour! So I check again and
	   force the policy if it is not set correctly. This does not
	   really really work either, the manager thread of the
	   linuxthreads implementation is left running with
	   SCHED_OTHER, that is presumably very bad.
	*/
	memset (&client_param, 0, sizeof (client_param));
	if (pthread_getschedparam(client->thread, &policy,
				  &client_param) == 0) {
		if (policy != SCHED_FIFO) {
			memset (&client_param, 0, sizeof (client_param));
			client_param.sched_priority =
				client->engine->client_priority;
			if (pthread_setschedparam (client->thread, SCHED_FIFO,
						   &client_param)) {
				jack_error ("Cannot set (again) FIFO scheduling"
					    " class for RT thread\n");
				return -1;
			}
		}
	}
#endif /* JACK_USE_MACH_THREADS */

	return 0;
}

int 
jack_activate (jack_client_t *client)
{
	jack_request_t req;

	/* we need to scribble on our stack to ensure that its memory
	 * pages are actually mapped (more important for mlockall(2)
	 * usage in jack_start_thread())
	 */
         
	char buf[JACK_THREAD_STACK_TOUCH];
	int i;

	for (i = 0; i < JACK_THREAD_STACK_TOUCH; i++) {
		buf[i] = (char) (i & 0xff);
	}

	if (client->control->type == ClientInternal ||
	    client->control->type == ClientDriver) {
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

			/* what to do? engine is running realtime, it
			   is using capabilities and has them
			   (otherwise we would not get an error
			   return) but for some reason it could not
			   give the client the required capabilities,
			   so for now downgrade the client so that it
			   still runs, albeit non-realtime - nando
			*/

			jack_error ("could not receive realtime capabilities, "
				    "client will run non-realtime");
			/* XXX wrong, this is a property of the engine
			client->engine->real_time = 0;
			*/
		}
	}
#endif /* USE_CAPABILITIES */

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

#if JACK_USE_MACH_THREADS 
		if (client->rt_thread_ok) {
			// MacOSX pthread_cancel not implemented in
			// Darwin 5.5, 6.4
			mach_port_t machThread =
				pthread_mach_thread_np (client->process_thread);
			thread_terminate (machThread);
		}
#endif
	
		/* stop the thread that communicates with the jack
		 * server, only if it was actually running 
		 */
		
		if (client->thread_ok){
			pthread_cancel (client->thread);
			pthread_join (client->thread, &status);
		}

		if (client->control) {
			jack_release_shm (&client->control_shm);
			client->control = NULL;
		}
		if (client->engine) {
			jack_release_shm (&client->engine_shm);
			client->engine = NULL;
		}

		if (client->port_segment) {
			jack_port_type_id_t ptid;
			for (ptid = 0; ptid < client->n_port_types; ++ptid) {
				jack_release_shm (&client->port_segment[ptid]);
			}
			free (client->port_segment);
			client->port_segment = NULL;
		}

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

int 
jack_is_realtime (jack_client_t *client)
{
	return client->engine->real_time;
}

jack_nframes_t 
jack_get_buffer_size (jack_client_t *client)
{
	return client->engine->buffer_size;
}

int
jack_set_buffer_size (jack_client_t *client, jack_nframes_t nframes)
{
#ifdef DO_BUFFER_RESIZE
	jack_request_t req;

	req.type = SetBufferSize;
	req.x.nframes = nframes;

	return jack_client_deliver_request (client, &req);
#else
	return ENOSYS;

#endif /* DO_BUFFER_RESIZE */
}

int 
jack_connect (jack_client_t *client, const char *source_port,
	      const char *destination_port)
{
	jack_request_t req;

	req.type = ConnectPorts;

	snprintf (req.x.connect.source_port,
		  sizeof (req.x.connect.source_port), "%s", source_port);
	snprintf (req.x.connect.destination_port,
		  sizeof (req.x.connect.destination_port),
		  "%s", destination_port);

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
jack_disconnect (jack_client_t *client, const char *source_port,
		 const char *destination_port)
{
	jack_request_t req;

	req.type = DisconnectPorts;

	snprintf (req.x.connect.source_port,
		  sizeof (req.x.connect.source_port), "%s", source_port);
	snprintf (req.x.connect.destination_port,
		  sizeof (req.x.connect.destination_port),
		  "%s", destination_port);
	
	return jack_client_deliver_request (client, &req);
}

void
jack_set_error_function (void (*func) (const char *))
{
	jack_error_callback = func;
}


int 
jack_set_graph_order_callback (jack_client_t *client,
			       JackGraphOrderCallback callback, void *arg)
{
	if (client->control->active) {
		jack_error ("You cannot set callbacks on an active client.");
		return -1;
	}
	client->control->graph_order = callback;
	client->control->graph_order_arg = arg;
	return 0;
}

int jack_set_xrun_callback (jack_client_t *client,
			    JackXRunCallback callback, void *arg)
{
	if (client->control->active) {
		jack_error ("You cannot set callbacks on an active client.");
		return -1;
	}

	client->control->xrun = callback;
	client->control->xrun_arg = arg;
	return 0;       
}

int
jack_set_process_callback (jack_client_t *client,
			   JackProcessCallback callback, void *arg)

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
jack_set_thread_init_callback (jack_client_t *client,
			       JackThreadInitCallback callback, void *arg)

{
	if (client->control->active) {
		jack_error ("You cannot set callbacks on an active client.");
		return -1;
	}
	client->control->thread_init_arg = arg;
	client->control->thread_init = callback;
	return 0;
}

int
jack_set_freewheel_callback (jack_client_t *client,
			     JackFreewheelCallback callback, void *arg)
{
	if (client->control->active) {
		jack_error ("You cannot set callbacks on an active client.");
		return -1;
	}
	client->control->freewheel_arg = arg;
	client->control->freewheel_cb = callback;
	return 0;
}

int
jack_set_buffer_size_callback (jack_client_t *client,
			       JackBufferSizeCallback callback, void *arg)
{
	client->control->bufsize_arg = arg;
	client->control->bufsize = callback;
	return 0;
}

int
jack_set_port_registration_callback(jack_client_t *client,
				    JackPortRegistrationCallback callback,
				    void *arg)
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
		regcomp (&port_regex, port_name_pattern,
			 REG_EXTENDED|REG_NOSUB);
	}
	if (type_name_pattern && type_name_pattern[0]) {
		regcomp (&type_regex, type_name_pattern,
			 REG_EXTENDED|REG_NOSUB);
	}

	psp = engine->ports;
	match_cnt = 0;

	matching_ports = (const char **)
		malloc (sizeof (char *) * engine->port_max);

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
			jack_port_type_id_t ptid = psp[i].ptype_id;
			if (regexec (&type_regex,
				     engine->port_types[ptid].type_name,
				     0, NULL, 0)) {
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

int
jack_client_name_size(void)
{
	return JACK_CLIENT_NAME_SIZE;
}

int
jack_port_name_size(void)
{
	return JACK_PORT_NAME_SIZE;
}

int
jack_port_type_size(void)
{
	return JACK_PORT_TYPE_SIZE;
}
