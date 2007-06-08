/* -*- mode: c; c-file-style: "bsd"; -*- */
/*
 *  Client creation and destruction interfaces for JACK engine.
 *
 *  Copyright (C) 2001-2003 Paul Davis
 *  Copyright (C) 2004 Jack O'Quin
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <config.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <jack/internal.h>
#include <jack/engine.h>
#include <jack/messagebuffer.h>
#include <jack/version.h>
#include <sysdeps/poll.h>
#include <sysdeps/ipc.h>

#include "clientengine.h"
#include "transengine.h"

#define JACK_ERROR_WITH_SOCKETS 10000000

static void
jack_client_disconnect_ports (jack_engine_t *engine,
			      jack_client_internal_t *client)
{
	JSList *node;
	jack_port_internal_t *port;

	/* call tree **** MUST HOLD *** engine->client_lock */

	for (node = client->ports; node; node = jack_slist_next (node)) {
		port = (jack_port_internal_t *) node->data;
		jack_port_clear_connections (engine, port);
		jack_port_registration_notify (engine, port->shared->id, FALSE);
		jack_port_release (engine, port);
	}

	jack_slist_free (client->ports);
	jack_slist_free (client->truefeeds);
	jack_slist_free (client->sortfeeds);
	client->truefeeds = 0;
	client->sortfeeds = 0;
	client->ports = 0;
}			

int
jack_client_do_deactivate (jack_engine_t *engine,
			   jack_client_internal_t *client, int sort_graph)
{
	/* caller must hold engine->client_lock and must have checked for and/or
	 *   cleared all connections held by client. */
	client->control->active = FALSE;

	jack_transport_client_exit (engine, client);

	if (!jack_client_is_internal (client) &&
	    engine->external_client_cnt > 0) {	
		engine->external_client_cnt--;
	}

	if (sort_graph) {
		jack_sort_graph (engine);
	}
	return 0;
}

static void
jack_zombify_client (jack_engine_t *engine, jack_client_internal_t *client)
{
	VERBOSE (engine, "removing client \"%s\" from the processing chain\n",
		 client->control->name);

	/* caller must hold the client_lock */

	/* this stops jack_deliver_event() from doing anything */

	client->control->dead = TRUE;

	jack_client_disconnect_ports (engine, client);
	jack_client_do_deactivate (engine, client, FALSE);
}

static void
jack_remove_client (jack_engine_t *engine, jack_client_internal_t *client)
{
	/* called *without* the request_lock */
	unsigned int i;
	JSList *node;

	/* caller must hold the client_lock */

	VERBOSE (engine, "removing client \"%s\"\n", client->control->name);

	/* if its not already a zombie, make it so */

	if (!client->control->dead) {
		jack_zombify_client (engine, client);
	}

	if (client->control->type == ClientExternal) {

		/* try to force the server thread to return from poll */
	
		close (client->event_fd);
		close (client->request_fd);

		/* rearrange the pollfd array so that things work right the 
		   next time we go into poll(2).
		*/
		
		for (i = 0; i < engine->pfd_max; i++) {
			if (engine->pfd[i].fd == client->request_fd) {
				if (i+1 < engine->pfd_max) {
					memmove (&engine->pfd[i],
						 &engine->pfd[i+1],
						 sizeof (struct pollfd)
						 * (engine->pfd_max - i));
				}
				engine->pfd_max--;
			}
		}
	}

	for (node = engine->clients; node; node = jack_slist_next (node)) {
		if (((jack_client_internal_t *) node->data)->control->id
		    == client->control->id) {
			engine->clients =
				jack_slist_remove_link (engine->clients, node);
			jack_slist_free_1 (node);
			break;
		}
	}

	jack_client_delete (engine, client);

	/* ignore the driver, which counts as a client. */
	if (engine->temporary && (jack_slist_length(engine->clients) <= 1)) {
		exit(0);
	}
	
}

void
jack_remove_clients (jack_engine_t* engine)
{
	JSList *tmp, *node;
	int need_sort = FALSE;
	jack_client_internal_t *client;
	
	/* remove all dead clients */

	for (node = engine->clients; node; ) {
		
		tmp = jack_slist_next (node);
		
		client = (jack_client_internal_t *) node->data;
		
		if (client->error) {
			
			/* if we have a communication problem with the
			   client, remove it. otherwise, turn it into
			   a zombie. the client will/should realize
			   this and will close its sockets.  then
			   we'll end up back here again and will
			   finally remove the client.
			*/
			if (client->error >= JACK_ERROR_WITH_SOCKETS) {
				VERBOSE (engine, "removing failed "
					 "client %s state = %s errors"
					 " = %d\n", 
					 client->control->name,
					 jack_client_state_name (client),
					 client->error);
				jack_remove_client (engine,
						    (jack_client_internal_t *)
						    node->data);
			} else {
				VERBOSE (engine, "client failure: "
					 "client %s state = %s errors"
					 " = %d\n", 
					 client->control->name,
					 jack_client_state_name (client),
					 client->error);
				if (!engine->nozombies) {
					jack_zombify_client (engine,
							     (jack_client_internal_t *)
							     node->data);
					client->error = 0;
				}
			}
			
			need_sort = TRUE;
		}
		
		node = tmp;
	}

	if (need_sort) {
		jack_sort_graph (engine);
	}
	
	jack_engine_reset_rolling_usecs (engine);
}

static int
jack_load_client (jack_engine_t *engine, jack_client_internal_t *client,
		  const char *so_name)
{
	const char *errstr;
	char path_to_so[PATH_MAX+1];

	snprintf (path_to_so, sizeof (path_to_so), ADDON_DIR "/%s.so", so_name);
	client->handle = dlopen (path_to_so, RTLD_NOW|RTLD_GLOBAL);
	
	if (client->handle == 0) {
		if ((errstr = dlerror ()) != 0) {
			jack_error ("%s", errstr);
		} else {
			jack_error ("bizarre error loading %s", so_name);
		}
		return -1;
	}

	client->initialize = dlsym (client->handle, "jack_initialize");

	if ((errstr = dlerror ()) != 0) {
		jack_error ("%s has no initialize() function\n", so_name);
		dlclose (client->handle);
		client->handle = 0;
		return -1;
	}

	client->finish = (void (*)(void *)) dlsym (client->handle,
						   "jack_finish");
	
	if ((errstr = dlerror ()) != 0) {
		jack_error ("%s has no finish() function", so_name);
		dlclose (client->handle);
		client->handle = 0;
		return -1;
	}

	return 0;
}

static void
jack_client_unload (jack_client_internal_t *client)
{
	if (client->handle) {
		if (client->finish) {
			client->finish (client->control->process_arg);
		}
		dlclose (client->handle);
	}
}

static jack_client_internal_t *
jack_client_by_name (jack_engine_t *engine, const char *name)
{
	jack_client_internal_t *client = NULL;
	JSList *node;

	jack_lock_graph (engine);

	for (node = engine->clients; node; node = jack_slist_next (node)) {
		if (strcmp ((const char *) ((jack_client_internal_t *)
					    node->data)->control->name,
			    name) == 0) {
			client = (jack_client_internal_t *) node->data;
			break;
		}
	}

	jack_unlock_graph (engine);
	return client;
}

static jack_client_id_t
jack_client_id_by_name (jack_engine_t *engine, const char *name)
{
	jack_client_id_t id = 0;	/* NULL client ID */
	JSList *node;

	jack_lock_graph (engine);

	for (node = engine->clients; node; node = jack_slist_next (node)) {
		if (strcmp ((const char *) ((jack_client_internal_t *)
					    node->data)->control->name,
			    name) == 0) {
			jack_client_internal_t *client = 
				(jack_client_internal_t *) node->data;
			id = client->control->id;
			break;
		}
	}

	jack_unlock_graph (engine);
	return id;
}

jack_client_internal_t *
jack_client_internal_by_id (jack_engine_t *engine, jack_client_id_t id)
{
	jack_client_internal_t *client = NULL;
	JSList *node;

	/* call tree ***MUST HOLD*** the graph lock */

	for (node = engine->clients; node; node = jack_slist_next (node)) {
		if (((jack_client_internal_t *) node->data)->control->id
		    == id) {
			client = (jack_client_internal_t *) node->data;
			break;
		}
	}

	return client;
}

/* generate a unique client name
 *
 * returns 0 if successful, updates name in place
 */
static inline int
jack_generate_unique_name (jack_engine_t *engine, char *name)
{
	int tens, ones;
	int length = strlen (name);

	if (length > JACK_CLIENT_NAME_SIZE - 4) {
		jack_error ("%s exists and is too long to make unique", name);
		return 1;		/* failure */
	}

	/*  generate a unique name by appending "-01".."-99" */
	name[length++] = '-';
	tens = length++;
	ones = length++;
	name[tens] = '0';
	name[ones] = '1';
	name[length] = '\0';
	while (jack_client_by_name (engine, name)) {
		if (name[ones] == '9') {
			if (name[tens] == '9') {
				jack_error ("client %s has 99 extra"
					    " instances already", name);
				return 1; /* give up */
			}
			name[tens]++;
			name[ones] = '0';
		} else {
			name[ones]++;
		}
	}
	return 0;
}

static int
jack_client_name_invalid (jack_engine_t *engine, char *name,
			  jack_options_t options, jack_status_t *status)
{
	/* Since this is always called from the server thread, no
	 * other new client will be created at the same time.  So,
	 * testing a name for uniqueness is valid here.  When called
	 * from jack_engine_load_driver() this is not strictly true,
	 * but that seems to be adequately serialized due to engine
	 * startup.  There are no other clients at that point, anyway.
	 */

	if (jack_client_by_name (engine, name)) {

		*status |= JackNameNotUnique;

		if (options & JackUseExactName) {
			jack_error ("cannot create new client; %s already"
				    " exists", name);
			*status |= JackFailure;
			return TRUE;
		}

		if (jack_generate_unique_name(engine, name)) {
			*status |= JackFailure;
			return TRUE;
		}
	}

	return FALSE;
}

/* Set up the engine's client internal and control structures for both
 * internal and external clients. */
static jack_client_internal_t *
jack_setup_client_control (jack_engine_t *engine, int fd,
			   ClientType type, const char *name)
{
	jack_client_internal_t *client;

	client = (jack_client_internal_t *)
		malloc (sizeof (jack_client_internal_t));

	client->request_fd = fd;
	client->event_fd = -1;
	client->ports = 0;
	client->truefeeds = 0;
	client->sortfeeds = 0;
	client->execution_order = UINT_MAX;
	client->next_client = NULL;
	client->handle = NULL;
	client->finish = NULL;
	client->error = 0;

	if (type != ClientExternal) {
		
		client->control = (jack_client_control_t *)
			malloc (sizeof (jack_client_control_t));		

	} else {

                if (jack_shmalloc (sizeof (jack_client_control_t), 
				   &client->control_shm)) {
                        jack_error ("cannot create client control block for %s",
				    name);
			free (client);
                        return 0;
                }

		if (jack_attach_shm (&client->control_shm)) {
			jack_error ("cannot attach to client control block "
				    "for %s (%s)", name, strerror (errno));
			jack_destroy_shm (&client->control_shm);
			free (client);
			return 0;
		}

		client->control = (jack_client_control_t *)
			jack_shm_addr (&client->control_shm);
	}

	client->control->type = type;
	client->control->active = 0;
	client->control->dead = FALSE;
	client->control->timed_out = 0;
	client->control->id = engine->next_client_id++;
	strcpy ((char *) client->control->name, name);
	client->subgraph_start_fd = -1;
	client->subgraph_wait_fd = -1;

	client->control->process = NULL;
	client->control->process_arg = NULL;
	client->control->bufsize = NULL;
	client->control->bufsize_arg = NULL;
	client->control->srate = NULL;
	client->control->srate_arg = NULL;
	client->control->xrun = NULL;
	client->control->xrun_arg = NULL;
	client->control->port_register = NULL;
	client->control->port_register_arg = NULL;
	client->control->port_connect = NULL;
	client->control->port_connect_arg = NULL;
	client->control->graph_order = NULL;
	client->control->graph_order_arg = NULL;
	client->control->client_register = NULL;
	client->control->client_register_arg = NULL;

	jack_transport_client_new (client);
        
#ifdef JACK_USE_MACH_THREADS
        /* specific resources for server/client real-time thread
	 * communication */
        allocate_mach_serverport(engine, client);
        client->running = FALSE;
#endif

	return client;
}

/* set up all types of clients */
static jack_client_internal_t *
setup_client (jack_engine_t *engine, ClientType type, char *name,
	      jack_options_t options, jack_status_t *status, int client_fd,
	      const char *object_path, const char *object_data)
{
	/* called with the request_lock */
	jack_client_internal_t *client;

	/* validate client name, generate a unique one if appropriate */
	if (jack_client_name_invalid (engine, name, options, status))
		return NULL;

	/* create a client struct for this name */
	if ((client = jack_setup_client_control (engine, client_fd,
						 type, name)) == NULL) {
		*status |= (JackFailure|JackInitFailure);
		jack_error ("cannot create new client object");
		return NULL;
	}

	/* only for internal clients, driver is already loaded */
	if (type == ClientInternal) {
		if (jack_load_client (engine, client, object_path)) {
			jack_error ("cannot dynamically load client from"
				    " \"%s\"", object_path);
			jack_client_delete (engine, client);
			*status |= (JackFailure|JackLoadFailure);
			return NULL;
		}
	}

	VERBOSE (engine, "new client: %s, id = %" PRIu32
		 " type %d @ %p fd = %d\n", 
		 client->control->name, client->control->id, 
		 type, client->control, client_fd);

	if (jack_client_is_internal(client)) {

		/* Set up the pointers necessary for the request
		 * system to work.  The client is in the same address
		 * space */

		client->control->deliver_request = internal_client_request;
		client->control->deliver_arg = engine;
	}

	/* add new client to the clients list */
	jack_lock_graph (engine);
 	engine->clients = jack_slist_prepend (engine->clients, client);
	jack_engine_reset_rolling_usecs (engine);
	
	if (jack_client_is_internal(client)) {

		/* Internal clients need to make regular JACK API
		 * calls, which need a jack_client_t structure.
		 * Create one here.
		 */
		client->control->private_client =
			jack_client_alloc_internal (client->control, engine);

		jack_unlock_graph (engine);

		/* Call its initialization function.  This function
		 * may make requests of its own, so we temporarily
		 * release and then reacquire the request_lock.  */
		if (client->control->type == ClientInternal) {

			pthread_mutex_unlock (&engine->request_lock);
			if (client->initialize (client->control->private_client,
						object_data)) {

				/* failed: clean up client data */
				VERBOSE (engine,
					 "%s jack_initialize() failed!\n",
					 client->control->name);
				jack_lock_graph (engine);
				jack_remove_client (engine, client);
				jack_unlock_graph (engine);
				*status |= (JackFailure|JackInitFailure);
				client = NULL;
				//JOQ: not clear that all allocated
				//storage has been cleaned up properly.
			}
			pthread_mutex_lock (&engine->request_lock);
		}

	} else {			/* external client */
		
		if (engine->pfd_max >= engine->pfd_size) {
			engine->pfd = (struct pollfd *)
				realloc (engine->pfd, sizeof (struct pollfd)
					 * (engine->pfd_size + 16));
			engine->pfd_size += 16;
		}
		
		engine->pfd[engine->pfd_max].fd = client->request_fd;
		engine->pfd[engine->pfd_max].events =
			POLLIN|POLLPRI|POLLERR|POLLHUP|POLLNVAL;
		engine->pfd_max++;

		jack_unlock_graph (engine);
	}
	
	return client;
}

jack_client_internal_t *
jack_create_driver_client (jack_engine_t *engine, char *name)
{
	jack_client_connect_request_t req;
	jack_status_t status;
	jack_client_internal_t *client;

	snprintf (req.name, sizeof (req.name), "%s", name);

	pthread_mutex_lock (&engine->request_lock);
	client = setup_client (engine, ClientDriver, name, JackUseExactName,
			       &status, -1, NULL, NULL);
	pthread_mutex_unlock (&engine->request_lock);

	return client;
}

static jack_status_t
handle_unload_client (jack_engine_t *engine, jack_client_id_t id)
{
	/* called *without* the request_lock */
	jack_client_internal_t *client;
	jack_status_t status = (JackNoSuchClient|JackFailure);

	jack_lock_graph (engine);

	if ((client = jack_client_internal_by_id (engine, id))) {
		VERBOSE (engine, "unloading client \"%s\"\n",
			 client->control->name);
		jack_remove_client (engine, client);
		status = 0;
	}

	jack_unlock_graph (engine);

	return status;
}

int
jack_client_create (jack_engine_t *engine, int client_fd)
{
	/* called *without* the request_lock */
	jack_client_internal_t *client;
	jack_client_connect_request_t req;
	jack_client_connect_result_t res;
	ssize_t nbytes;

	res.status = 0;

	nbytes = read (client_fd, &req, sizeof (req));

	if (nbytes == 0) {		/* EOF? */
		jack_error ("cannot read connection request from client");
		return -1;
	}

	/* First verify protocol version (first field of request), if
	 * present, then make sure request has the expected length. */
	if ((nbytes < sizeof (req.protocol_v))
	    || (req.protocol_v != jack_protocol_version)
	    || (nbytes != sizeof (req))) {

		/* JACK protocol incompatibility */
		res.status |= (JackFailure|JackVersionError);
		jack_error ("JACK protocol mismatch (%d vs %d)", req.protocol_v, jack_protocol_version);
		if (write (client_fd, &res, sizeof (res)) != sizeof (res)) {
			jack_error ("cannot write client connection response");
		}
		return -1;
	}

	if (!req.load) {		/* internal client close? */

		int rc = -1;
		jack_client_id_t id;

		if ((id = jack_client_id_by_name(engine, req.name))) {
			rc = handle_unload_client (engine, id);
		}
		
		/* close does not send a reply */
		return rc;
	}
	
	pthread_mutex_lock (&engine->request_lock);
	client = setup_client (engine, req.type, req.name,
			       req.options, &res.status, client_fd,
			       req.object_path, req.object_data);
	pthread_mutex_unlock (&engine->request_lock);
	if (client == NULL) {
		res.status |= JackFailure; /* just making sure */
		return -1;
	}
	res.client_shm = client->control_shm;
	res.engine_shm = engine->control_shm;
	res.realtime = engine->control->real_time;
	res.realtime_priority = engine->rtpriority - 1;
	strncpy (res.name, req.name, sizeof(res.name));

#ifdef JACK_USE_MACH_THREADS
	/* Mach port number for server/client communication */
	res.portnum = client->portnum;
#endif
	
	if (jack_client_is_internal(client)) {
		res.client_control = client->control;
		res.engine_control = engine->control;
	} else {
		strcpy (res.fifo_prefix, engine->fifo_prefix);
	}

	if (write (client_fd, &res, sizeof (res)) != sizeof (res)) {
		jack_error ("cannot write connection response to client");
		jack_client_delete (engine, client);
		return -1;
	}

	if (jack_client_is_internal (client)) {
		close (client_fd);
	}

	jack_client_registration_notify (engine, (const char*) client->control->name, 1);

	return 0;
}

int
jack_client_activate (jack_engine_t *engine, jack_client_id_t id)
{
	jack_client_internal_t *client;
	JSList *node;
	int ret = -1;

	jack_lock_graph (engine);

	for (node = engine->clients; node; node = jack_slist_next (node)) {

		if (((jack_client_internal_t *) node->data)->control->id
		    == id) {
		       
			client = (jack_client_internal_t *) node->data;
			client->control->active = TRUE;

			jack_transport_activate(engine, client);

			/* we call this to make sure the FIFO is
			 * built+ready by the time the client needs
			 * it. we don't care about the return value at
			 * this point.
			 */

			jack_get_fifo_fd (engine,
					  ++engine->external_client_cnt);
			jack_sort_graph (engine);

			ret = 0;
			break;
		}
	}

	jack_unlock_graph (engine);
	return ret;
}	

int
jack_client_deactivate (jack_engine_t *engine, jack_client_id_t id)
{
	JSList *node;
	int ret = -1;

	jack_lock_graph (engine);

	for (node = engine->clients; node; node = jack_slist_next (node)) {

		jack_client_internal_t *client =
			(jack_client_internal_t *) node->data;

		if (client->control->id == id) {
		        
	        	JSList *portnode;
			jack_port_internal_t *port;

			for (portnode = client->ports; portnode;
			     portnode = jack_slist_next (portnode)) {
				port = (jack_port_internal_t *) portnode->data;
				jack_port_clear_connections (engine, port);
 			}

			ret = jack_client_do_deactivate (engine, client, TRUE);
			break;
		}
	}

	jack_unlock_graph (engine);

	return ret;
}	

int
jack_client_disconnect (jack_engine_t *engine, int fd)
{
	jack_client_internal_t *client = 0;
	JSList *node;

#ifndef DEFER_CLIENT_REMOVE_TO_AUDIO_THREAD

        jack_lock_graph (engine);

        for (node = engine->clients; node; node = jack_slist_next (node)) {

                if (jack_client_is_internal((jack_client_internal_t *)
					    node->data)) {
                        continue;
                }

                if (((jack_client_internal_t *) node->data)->request_fd == fd) {
                        client = (jack_client_internal_t *) node->data;
                        break;
                }
        }

        if (client) {
		VERBOSE (engine, "removing disconnected client %s state = "
			 "%s errors = %d\n", client->control->name,
			 jack_client_state_name (client),
			 client->error);
		jack_remove_client(engine, client);
		jack_sort_graph (engine);
	}

        jack_unlock_graph (engine);

#else /* DEFER_CLIENT_REMOVE_TO_AUDIO_THREAD */

	jack_lock_graph (engine);

	for (node = engine->clients; node; node = jack_slist_next (node)) {

		if (jack_client_is_internal((jack_client_internal_t *)
					    node->data)) {
			continue;
		}

		if (((jack_client_internal_t *) node->data)->request_fd == fd) {
			client = (jack_client_internal_t *) node->data;
			if (client->error < JACK_ERROR_WITH_SOCKETS) {
				client->error += JACK_ERROR_WITH_SOCKETS;
			}
			break;
		}
	}

	jack_unlock_graph (engine);

#endif /* DEFER_CLIENT_REMOVE_TO_AUDIO_THREAD */

	return 0;
}

void
jack_client_delete (jack_engine_t *engine, jack_client_internal_t *client)
{
	jack_client_registration_notify (engine, (const char*) client->control->name, 0);

	if (jack_client_is_internal (client)) {

		jack_client_unload (client);
		free (client->control->private_client);
		free ((void *) client->control);

	} else {
		
		/* release the client segment, mark it for
		   destruction, and free up the shm registry
		   information so that it can be reused.
		*/

		jack_release_shm (&client->control_shm);
		jack_destroy_shm (&client->control_shm);
	}

	free (client);
}

void
jack_intclient_handle_request (jack_engine_t *engine, jack_request_t *req)
{
	jack_client_internal_t *client;

	req->status = 0;
	if ((client = jack_client_by_name (engine, req->x.intclient.name))) {
		req->x.intclient.id = client->control->id;
	} else {
		req->status |= (JackNoSuchClient|JackFailure);
	}
}

void
jack_intclient_load_request (jack_engine_t *engine, jack_request_t *req)
{
	/* called with the request_lock */
	jack_client_internal_t *client;
	jack_status_t status = 0;

	VERBOSE (engine, "load internal client %s from %s, init `%s', "
		 "options: 0x%x\n", req->x.intclient.name,
		 req->x.intclient.path, req->x.intclient.init,
		 req->x.intclient.options);

	client = setup_client (engine, ClientInternal, req->x.intclient.name,
			       req->x.intclient.options, &status, -1,
			       req->x.intclient.path, req->x.intclient.init);

	if (client == NULL) {
		status |= JackFailure;	/* just making sure */
		req->x.intclient.id = 0;
		VERBOSE (engine, "load failed, status = 0x%x\n", status);
	} else {
		req->x.intclient.id = client->control->id;
	}

	req->status = status;
}

void
jack_intclient_name_request (jack_engine_t *engine, jack_request_t *req)
{
	jack_client_internal_t *client;

	jack_lock_graph (engine);
	if ((client = jack_client_internal_by_id (engine,
						  req->x.intclient.id))) {
		strncpy ((char *) req->x.intclient.name,
			 (char *) client->control->name,
			 sizeof (req->x.intclient.name));
		req->status = 0;
	} else {
		req->status = (JackNoSuchClient|JackFailure);
	}
	jack_unlock_graph (engine);
}

void
jack_intclient_unload_request (jack_engine_t *engine, jack_request_t *req)
{
	/* Called with the request_lock, but we need to call
	 * handle_unload_client() *without* it. */

	if (req->x.intclient.id) {
		pthread_mutex_unlock (&engine->request_lock);
		req->status =
			handle_unload_client (engine, req->x.intclient.id);
		pthread_mutex_lock (&engine->request_lock);
	} else {
		VERBOSE (engine, "invalid unload request\n");
		req->status = JackFailure;
	}
}
