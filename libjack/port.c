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

#include <string.h>
#include <stdio.h>
#include <math.h>

#include <config.h>

#include <jack/jack.h>
#include <jack/types.h>
#include <jack/internal.h>
#include <jack/engine.h>
#include <jack/pool.h>
#include <jack/port.h>
#include <jack/error.h>
#include <jack/jslist.h>

#include "local.h"

jack_port_t *
jack_port_new (const jack_client_t *client, jack_port_id_t port_id, jack_control_t *control)
{
	jack_port_t *port;
	jack_port_shared_t *shared;
	jack_port_segment_info_t *si;
	JSList *node;

	shared = &control->ports[port_id];

	port = (jack_port_t *) malloc (sizeof (jack_port_t));

	port->client_segment_base = 0;
	port->shared = shared;
	pthread_mutex_init (&port->connection_lock, NULL);
	port->connections = 0;
	port->shared->tied = NULL;
	port->shared->peak = port->shared->type_info.peak;
	port->shared->power = port->shared->type_info.power;
	
	si = NULL;
	
	for (node = client->port_segments; node; node = jack_slist_next (node)) {
		
		si = (jack_port_segment_info_t *) node->data;

		if (strcmp (si->shm_name, port->shared->shm_name) == 0) {
			fprintf (stderr, "found port segment for %s\n", port->shared->name);
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

	snprintf (req.x.port_info.type, sizeof (req.x.port_info.type), "%s", port_type);
	req.x.port_info.flags = flags;
	req.x.port_info.buffer_size = buffer_size;
	req.x.port_info.client_id = client->control->id;

	if (jack_client_deliver_request (client, &req)) {
		return NULL;
	}

	port = jack_port_new (client, req.x.port_info.port_id, client->engine);

	type_info = NULL;

	for (n = 0; jack_builtin_port_types[n].type_name[0]; n++) {
		
		if (strcmp (req.x.port_info.type, jack_builtin_port_types[n].type_name) == 0) {
			type_info = &jack_builtin_port_types[n];
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

	client->ports = jack_slist_prepend (client->ports, port);

	return port;
}

int 
jack_port_unregister (jack_client_t *client, jack_port_t *port)
{
	jack_request_t req;

	req.type = UnRegisterPort;
	req.x.port_info.port_id = port->shared->id;
	req.x.port_info.client_id = client->control->id;

	return jack_client_deliver_request (client, &req);
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

/* LOCAL (in-client) connection querying only */

int
jack_port_connected (const jack_port_t *port)
{
	return jack_slist_length (port->connections);
}

int
jack_port_connected_to (const jack_port_t *port, const char *portname)
{
	JSList *node;
	int ret = FALSE;

	/* XXX this really requires a cross-process lock
	   so that ports/connections cannot go away
	   while we are checking for them. that's hard,
	   and has a non-trivial performance impact
	   for jackd.
	*/  

	pthread_mutex_lock (&((jack_port_t *) port)->connection_lock);

	for (node = port->connections; node; node = jack_slist_next (node)) {
		jack_port_t *other_port = (jack_port_t *) node->data;
		
		if (strcmp (other_port->shared->name, portname) == 0) {
			ret = TRUE;
			break;
		}
	}

	pthread_mutex_unlock (&((jack_port_t *) port)->connection_lock);
	return ret;
}

const char **
jack_port_get_connections (const jack_port_t *port)
{
	const char **ret = NULL;
	JSList *node;
	unsigned int n;

	/* XXX this really requires a cross-process lock
	   so that ports/connections cannot go away
	   while we are checking for them. that's hard,
	   and has a non-trivial performance impact
	   for jackd.
	*/  

	pthread_mutex_lock (&((jack_port_t *) port)->connection_lock);

	if (port->connections != NULL) {

		ret = (const char **) malloc (sizeof (char *) * (jack_slist_length (port->connections) + 1));
		for (n = 0, node = port->connections; node; node = jack_slist_next (node), ++n) {
			ret[n] = ((jack_port_t *) node->data)->shared->name;
		}
		ret[n] = NULL;
	}

	pthread_mutex_unlock (&((jack_port_t *) port)->connection_lock);
	return ret;
}

/* SERVER-SIDE (all) connection querying */

const char **
jack_port_get_all_connections (const jack_client_t *client, const jack_port_t *port)
{
	const char **ret;
	jack_request_t req;
	unsigned int i;

	if (port == NULL) {
		return NULL;
	}

	req.type = GetPortConnections;

	req.x.port_info.name[0] = '\0';
	req.x.port_info.type[0] = '\0';
	req.x.port_info.flags = 0;
	req.x.port_info.buffer_size = 0;
	req.x.port_info.client_id = 0;
	req.x.port_info.port_id = port->shared->id;

	jack_client_deliver_request (client, &req);

	if (req.status != 0 || req.x.port_connections.nports == 0) {
		return NULL;
	}

	if (client->request_fd < 0) {
		/* internal client */
		return req.x.port_connections.ports;
	}

	ret = (const char **) malloc (sizeof (char *) * (req.x.port_connections.nports + 1));

	for (i = 0; i < req.x.port_connections.nports; ++i ) {
		jack_port_id_t port_id;
		
		if (read (client->request_fd, &port_id, sizeof (port_id)) != sizeof (port_id)) {
			jack_error ("cannot read port id from server");
			return 0;
		}
		
		ret[i] = jack_port_by_id (client, port_id)->shared->name;
	}

	ret[i] = NULL;

	return ret;
}

jack_port_t *
jack_port_by_id (const jack_client_t *client, jack_port_id_t id)
{
	JSList *node;

	for (node = client->ports; node; node = jack_slist_next (node)) {
		if (((jack_port_t *) node->data)->shared->id == id) {
			return (jack_port_t *) node->data;
		}
	}

	if (id >= client->engine->port_max)
		return NULL;

	if (client->engine->ports[id].in_use)
		return jack_port_new (client, id, client->engine);

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

jack_nframes_t
jack_port_get_latency (jack_port_t *port)
{
	return port->shared->latency;
}

jack_nframes_t
jack_port_get_total_latency (jack_client_t *client, jack_port_t *port)
{
	return port->shared->total_latency;
}

void
jack_port_set_latency (jack_port_t *port, jack_nframes_t nframes)
{
	port->shared->latency = nframes;
}

void *
jack_port_get_buffer (jack_port_t *port, jack_nframes_t nframes)
{
	JSList *node, *next;

	/* Output port. The buffer was assigned by the engine
	   when the port was registered.
	*/

	if (port->shared->flags & JackPortIsOutput) {
		if (port->shared->tied) {
			return jack_port_get_buffer (port->shared->tied, nframes);
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

	if ((next = jack_slist_next (node)) == NULL) {

		/* one connection: use zero-copy mode - just pass
		   the buffer of the connected (output) port.
		*/

		return jack_port_get_buffer (((jack_port_t *) node->data), nframes);
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
								 sizeof (jack_default_audio_sample_t) * nframes);
		port->client_segment_base = 0;
	}

	port->shared->type_info.mixdown (port, nframes);
	return (jack_default_audio_sample_t *) port->shared->offset;
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

	dst->shared->tied = src;
	return 0;
}

int
jack_port_untie (jack_port_t *port)

{
	if (port->shared->tied == NULL) {
		jack_error ("port \"%s\" is not tied", port->shared->name);
		return -1;
	}
	port->shared->tied = NULL;
	return 0;
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

		JSList *node;

		/* this port is for input, so recurse over each of the 
		   connected ports.
		 */

		pthread_mutex_lock (&port->connection_lock);
		for (node = port->connections; node; node = jack_slist_next (node)) {
			
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
jack_port_is_mine (const jack_client_t *client, const jack_port_t *port)
{
	return port->shared->client_id == client->control->id;
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
jack_port_set_peak_function (jack_port_t *port, double (*func)(jack_port_t* port, jack_nframes_t))
{
	port->shared->peak = func;
}

void
jack_port_set_power_function (jack_port_t *port, double (*func)(jack_port_t* port, jack_nframes_t))
{
	port->shared->power = func;
}

double
jack_port_get_peak (jack_port_t* port, jack_nframes_t nframes)
{
	if (port->shared->peak (port, nframes)) {
		return port->shared->peak (port, nframes);
	} else {
		return 0;
	}
}

double
jack_port_get_power (jack_port_t* port, jack_nframes_t nframes)
{
	if (port->shared->power) {
		return port->shared->power (port, nframes);
	} else {
		return 0;
	}
}

/* AUDIO PORT SUPPORT */

static inline float f_max(float x, float a)
{
	x -= a;
	x += fabs (x);
	x *= 0.5;
	x += a;

	return (x);
}

static double
jack_audio_port_peak (jack_port_t *port, jack_nframes_t nframes)
{
	jack_nframes_t n;
	jack_default_audio_sample_t *buf = (jack_default_audio_sample_t *) jack_port_get_buffer (port, nframes);
	float max = 0;

	for (n = 0; n < nframes; ++n) {
		max = f_max (buf[n], max);
	}

	return max;
}

static void 
jack_audio_port_mixdown (jack_port_t *port, jack_nframes_t nframes)
{
	JSList *node;
	jack_port_t *input;
	jack_nframes_t n;
	jack_default_audio_sample_t *buffer;
	jack_default_audio_sample_t *dst, *src;

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

	memcpy (buffer, jack_port_buffer (input), sizeof (jack_default_audio_sample_t) * nframes);

	for (node = jack_slist_next (node); node; node = jack_slist_next (node)) {

		input = (jack_port_t *) node->data;

		n = nframes;
		dst = buffer;
		src = jack_port_buffer (input);

		while (n--) {
			*dst++ += *src++;
		}
	}
}

jack_port_type_info_t jack_builtin_port_types[] = {
	{ .type_name = JACK_DEFAULT_AUDIO_TYPE, 
	  .mixdown = jack_audio_port_mixdown, 
	  .peak = jack_audio_port_peak,
	  .power = NULL,
	  .buffer_scale_factor = 1 
	},
	{ .type_name = "", 
	  .mixdown = NULL 
	}
};

