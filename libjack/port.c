/*
    Copyright (C) 2001-2003 Paul Davis
    Copyright (C) 2005 Jussi Laako
    
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

*/

#include <string.h>
#include <stdio.h>
#include <math.h>

#include <config.h>
#include <sys/mman.h>
#include <uuid/uuid.h>

#include <jack/jack.h>
#include <jack/types.h>
#include <jack/midiport.h>
#include <jack/uuid.h>

#include <jack/jslist.h>

#include "internal.h"
#include "engine.h"
#include "pool.h"
#include "port.h"
#include "intsimd.h"

#include "local.h"

static void    jack_generic_buffer_init(void *port_buffer,
                                      size_t buffer_size,
				      jack_nframes_t nframes);

static void    jack_audio_port_mixdown (jack_port_t *port,
					jack_nframes_t nframes);

/* These function pointers are local to each address space.  For
 * internal clients they reside within jackd; for external clients in
 * the application process. */
jack_port_functions_t jack_builtin_audio_functions = {
	.buffer_init    = jack_generic_buffer_init,
	.mixdown = jack_audio_port_mixdown, 
};

extern jack_port_functions_t jack_builtin_midi_functions;

jack_port_functions_t jack_builtin_NULL_functions = {
	.buffer_init    = jack_generic_buffer_init,
	.mixdown = NULL, 
};

/* Only the Audio and MIDI port types are currently built in. */
jack_port_type_info_t jack_builtin_port_types[] = {
	{ .type_name = JACK_DEFAULT_AUDIO_TYPE, 
	  .buffer_scale_factor = 1,
	},
	{ .type_name = JACK_DEFAULT_MIDI_TYPE, 
	  .buffer_scale_factor = -1,
          .buffer_size = 2048
	},
	{ .type_name = "", }
};

/* these functions have been taken from libDSP X86.c  -jl */

#ifdef USE_DYNSIMD

static void (*opt_copy) (float *, const float *, int);
static void (*opt_mix) (float *, const float *, int);

static void
gen_copyf (float *dest, const float *src, int length)
{
	memcpy(dest, src, length * sizeof(float));
}

static void
gen_mixf (float *dest, const float *src, int length)
{
	int n;

	n = length;
	while (n--)
		*dest++ += *src++;
	/*for (iSample = 0; iSample < iDataLength; iSample++)
		fpDest[iSample] += fpSrc[iSample];*/
}

#ifdef ARCH_X86

void jack_port_set_funcs ()
{
	if (ARCH_X86_HAVE_SSE2(cpu_type)) {
		opt_copy = x86_sse_copyf;
		opt_mix = x86_sse_add2f;
	}
	else if (ARCH_X86_HAVE_3DNOW(cpu_type)) {
		opt_copy = x86_3dnow_copyf;
		opt_mix = x86_3dnow_add2f;
	}
	else {
		opt_copy = gen_copyf;
		opt_mix = gen_mixf;
	}
}

#else /* ARCH_X86 */

void jack_port_set_funcs ()
{
	opt_copy = gen_copyf;
	opt_mix = gen_mixf;
}

#endif /* ARCH_X86 */

#endif /* USE_DYNSIMD */

int
jack_port_name_equals (jack_port_shared_t* port, const char* target)
{
	char buf[JACK_PORT_NAME_SIZE+1];

	/* this nasty, nasty kludge is here because between 0.109.0 and 0.109.1,
	   the ALSA audio backend had the name "ALSA", whereas as before and
	   after it, it was called "alsa_pcm". this stops breakage for
	   any setups that have saved "alsa_pcm" or "ALSA" in their connection
	   state.
	*/

	if (strncmp (target, "ALSA:capture", 12) == 0 || strncmp (target, "ALSA:playback", 13) == 0) {
		snprintf (buf, sizeof (buf), "alsa_pcm%s", target+4);
		target = buf;
	}

	return (strcmp (port->name, target) == 0 || 
		strcmp (port->alias1, target) == 0 || 
		strcmp (port->alias2, target) == 0);
}

jack_port_functions_t *
jack_get_port_functions(jack_port_type_id_t ptid)
{
	switch (ptid) {
	case JACK_AUDIO_PORT_TYPE:
		return &jack_builtin_audio_functions;
	case JACK_MIDI_PORT_TYPE:
		return &jack_builtin_midi_functions;
	/* no other builtin functions */
	default:
		return NULL;
	}
}

/*
 * Fills buffer with zeroes. For audio ports, engine->silent_buffer relies on it.
 */
static void
jack_generic_buffer_init(void *buffer, size_t size, jack_nframes_t nframes)
{ 
	memset(buffer, 0, size);
}


jack_port_t *
jack_port_new (const jack_client_t *client, jack_port_id_t port_id,
	       jack_control_t *control)
{
	jack_port_shared_t *shared = &control->ports[port_id];
	jack_port_type_id_t ptid = shared->ptype_id;
	jack_port_t *port;

	if ((port = (jack_port_t *) malloc (sizeof (jack_port_t))) == NULL) {
		return NULL;
	}
	
	port->mix_buffer = NULL;
	port->client_segment_base = NULL;
	port->shared = shared;
	port->type_info = &client->engine->port_types[ptid];
	pthread_mutex_init (&port->connection_lock, NULL);
	port->connections = 0;
	port->tied = NULL;

	if (jack_uuid_compare (client->control->uuid, port->shared->client_id) == 0) {
			
		/* It's our port, so initialize the pointers to port
		 * functions within this address space.  These builtin
		 * definitions can be overridden by the client. 
		 */
		jack_port_functions_t *port_functions = jack_get_port_functions(ptid);
		if (port_functions == NULL)
			port_functions = &jack_builtin_NULL_functions;
		port->fptr = *port_functions;
		port->shared->has_mixdown = (port->fptr.mixdown ? TRUE : FALSE);
	}

	/* set up a base address so that port->offset can be used to
	   compute the correct location. we don't store the location
	   directly, because port->client_segment_base and/or
	   port->offset can change if the buffer size or port counts
	   are changed.
	*/

	port->client_segment_base =
		(void **) &client->port_segment[ptid].attached_at;

	return port;
}

size_t 
jack_port_type_get_buffer_size (jack_client_t *client, const char *port_type)
{
	int i;

	for (i=0; i<client->engine->n_port_types; i++) {
		if (!strcmp(port_type, client->engine->port_types[i].type_name))
			break;
	}

	if (i==client->engine->n_port_types)
		return 0;

	return jack_port_type_buffer_size (&(client->engine->port_types[i]), client->engine->buffer_size);
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
	int length ;

        VALGRIND_MEMSET (&req, 0, sizeof (req));

	req.type = RegisterPort;

	length = strlen ((const char *) client->control->name)
		+ 1 + strlen (port_name);
	if ( length >= sizeof (req.x.port_info.name) ) {
	  jack_error ("\"%s:%s\" is too long to be used as a JACK port name.\n"
		      "Please use %lu characters or less.",
		      client->control->name , 
		      port_name,
		      sizeof (req.x.port_info.name) - 1);
	  return NULL ;
	}

	strcpy ((char *) req.x.port_info.name,
		(const char *) client->control->name);
	strcat ((char *) req.x.port_info.name, ":");
	strcat ((char *) req.x.port_info.name, port_name);

	snprintf (req.x.port_info.type, sizeof (req.x.port_info.type),
		  "%s", port_type);
	req.x.port_info.flags = flags;
	req.x.port_info.buffer_size = buffer_size;
	jack_uuid_copy (&req.x.port_info.client_id, client->control->uuid);

	if (jack_client_deliver_request (client, &req)) {
		jack_error ("cannot deliver port registration request");
		return NULL;
	}

	if ((port = jack_port_new (client, req.x.port_info.port_id,
				   client->engine)) == NULL) {
		jack_error ("cannot allocate client side port structure");
		return NULL;
	}

	client->ports = jack_slist_prepend (client->ports, port);

	return port;
}

int 
jack_port_unregister (jack_client_t *client, jack_port_t *port)
{
	jack_request_t req;
        
        VALGRIND_MEMSET (&req, 0, sizeof (req));


	req.type = UnRegisterPort;
	req.x.port_info.port_id = port->shared->id;
	jack_uuid_copy (&req.x.port_info.client_id, client->control->uuid);

	return jack_client_deliver_request (client, &req);
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
		
		if (jack_port_name_equals (other_port->shared, portname)) {
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

		ret = (const char **)
			malloc (sizeof (char *)
				* (jack_slist_length (port->connections) + 1));
		if (ret == NULL) {
			pthread_mutex_unlock (&((jack_port_t *)port)->connection_lock);
			return NULL;
		}

		for (n = 0, node = port->connections; node;
		     node = jack_slist_next (node), ++n) {
			jack_port_t* other =(jack_port_t *) node->data;
			ret[n] = other->shared->name;
		}
		ret[n] = NULL;
	}

	pthread_mutex_unlock (&((jack_port_t *) port)->connection_lock);
	return ret;
}

/* SERVER-SIDE (all) connection querying */

const char **
jack_port_get_all_connections (const jack_client_t *client,
			       const jack_port_t *port)
{
	const char **ret;
	jack_request_t req;
	jack_port_t *tmp;
	unsigned int i;
	int need_free = FALSE;

	if (port == NULL) {
		return NULL;
	}

        VALGRIND_MEMSET (&req, 0, sizeof (req));
		
	req.type = GetPortConnections;

	req.x.port_info.name[0] = '\0';
	req.x.port_info.type[0] = '\0';
	req.x.port_info.flags = 0;
	req.x.port_info.buffer_size = 0;
	jack_uuid_clear (&req.x.port_info.client_id);
	req.x.port_info.port_id = port->shared->id;

	jack_client_deliver_request (client, &req);

	if (req.status != 0 || req.x.port_connections.nports == 0) {
		return NULL;
	}

	if (client->request_fd < 0) {
		/* internal client, .ports is in our own address space */
		return req.x.port_connections.ports;
	}

	if ((ret = (const char **) malloc (sizeof (char *) * (req.x.port_connections.nports + 1))) == NULL) {
		return NULL;
	}

	for (i = 0; i < req.x.port_connections.nports; ++i ) {
		jack_port_id_t port_id;
		
		if (read (client->request_fd, &port_id, sizeof (port_id))
		    != sizeof (port_id)) {
			jack_error ("cannot read port id from server");
			return 0;
		}
		tmp = jack_port_by_id_int (client, port_id, &need_free);
		ret[i] = tmp->shared->name;
		if (need_free) {
			free (tmp);
			need_free = FALSE;
		}
	}

	ret[i] = NULL;

	return ret;
}

jack_port_t *
jack_port_by_id_int (const jack_client_t *client, jack_port_id_t id, int* free)
{
	JSList *node;

	for (node = client->ports; node; node = jack_slist_next (node)) {
		if (((jack_port_t *) node->data)->shared->id == id) {
			*free = FALSE;
			return (jack_port_t *) node->data;
		}
	}

	if (id >= client->engine->port_max)
		return NULL;

	if (client->engine->ports[id].in_use) {
		*free = TRUE;
		return jack_port_new (client, id, client->engine);
	}

	return NULL;
}

jack_port_t *
jack_port_by_id (jack_client_t *client, jack_port_id_t id)
{
	JSList *node;
	jack_port_t* port;
	int need_free = FALSE;
	for (node = client->ports_ext; node; node = jack_slist_next (node)) {
		port = node->data;
		if (port->shared->id == id) { // Found port, return the cached structure
			return port;
		}
	}
	
	// Otherwise possibly allocate a new port structure, keep it in the ports_ext list for later use
	port = jack_port_by_id_int (client,id,&need_free);
	if (port != NULL && need_free)
		client->ports_ext =
			jack_slist_prepend (client->ports_ext, port);
	return port;
}

jack_port_t *
jack_port_by_name_int (jack_client_t *client, const char *port_name)
{
	unsigned long i, limit;
	jack_port_shared_t *port;
	
	limit = client->engine->port_max;
	port = &client->engine->ports[0];
	
	for (i = 0; i < limit; i++) {
		if (port[i].in_use && jack_port_name_equals (&port[i], port_name)) {
			return jack_port_new (client, port[i].id,
					      client->engine);
		}
	}

	return NULL;
}

jack_port_t *
jack_port_by_name (jack_client_t *client,  const char *port_name)
{
	JSList *node;
	jack_port_t* port;
	for (node = client->ports_ext; node; node = jack_slist_next (node)) {
		port = node->data;
		if (jack_port_name_equals (port->shared, port_name)) {
			/* Found port, return the cached structure. */
			return port;
		}
	}
	
	/* Otherwise allocate a new port structure, keep it in the
	 * ports_ext list for later use. */
	port = jack_port_by_name_int (client, port_name);
	if (port != NULL)
		client->ports_ext =
			jack_slist_prepend (client->ports_ext, port);
	return port;
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
	
	/* setup the new latency values here,
	 * so we dont need to change the backend codes.
	 */
	if (port->shared->flags & JackPortIsOutput) {
		port->shared->capture_latency.min = nframes;
		port->shared->capture_latency.max = nframes;
	}
	if (port->shared->flags & JackPortIsInput) {
		port->shared->playback_latency.min = nframes;
		port->shared->playback_latency.max = nframes;
	}
}

void *
jack_port_get_buffer (jack_port_t *port, jack_nframes_t nframes)
{
	JSList *node, *next;

	/* Output port.  The buffer was assigned by the engine
	   when the port was registered.
	*/
	if (port->shared->flags & JackPortIsOutput) {
		if (port->tied) {
			return jack_port_get_buffer (port->tied, nframes);
		}
                
                if (port->client_segment_base == NULL || *port->client_segment_base == MAP_FAILED) {
                        return NULL;
                }

		return jack_output_port_buffer (port);
	}

	/* Input port.  Since this can only be called from the
	   process() callback, and since no connections can be
	   made/broken during this phase (enforced by the jack
	   server), there is no need to take the connection lock here
	*/
	if ((node = port->connections) == NULL) {
		
                if (port->client_segment_base == NULL || *port->client_segment_base == MAP_FAILED) {
                        return NULL;
                }

		/* no connections; return a zero-filled buffer */
		return (void *) (*(port->client_segment_base) + port->type_info->zero_buffer_offset);
	}

	if ((next = jack_slist_next (node)) == NULL) {

		/* one connection: use zero-copy mode - just pass
		   the buffer of the connected (output) port.
		*/
		return jack_port_get_buffer (((jack_port_t *) node->data),
					     nframes);
	}

	/* Multiple connections.  Use a local buffer and mix the
	   incoming data into that buffer.  We have already
	   established the existence of a mixdown function during the
	   connection process.
	*/
	if (port->mix_buffer == NULL) {
		jack_error( "internal jack error: mix_buffer not allocated" );
		return NULL;
	}
	port->fptr.mixdown (port, nframes);
	return (void *) port->mix_buffer;
}

size_t
jack_port_type_buffer_size (jack_port_type_info_t* port_type_info, jack_nframes_t nframes)
{
	if( port_type_info->buffer_scale_factor < 0 ) {
		return port_type_info->buffer_size;
	} 

	return port_type_info->buffer_scale_factor
		* sizeof (jack_default_audio_sample_t)
		* nframes;
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
		for (node = port->connections; node;
		     node = jack_slist_next (node)) {
			
			/* drop the lock because if there is a feedback loop,
			   we will deadlock. XXX much worse things will
			   happen if there is a feedback loop !!!
			*/

			pthread_mutex_unlock (&port->connection_lock);
			jack_port_request_monitor ((jack_port_t *) node->data,
						   onoff);
			pthread_mutex_lock (&port->connection_lock);
		}
		pthread_mutex_unlock (&port->connection_lock);
	}

	return 0;
}
	
int 
jack_port_request_monitor_by_name (jack_client_t *client,
				   const char *port_name, int onoff)

{
	jack_port_t *port;
	unsigned long i, limit;
	jack_port_shared_t *ports;

	limit = client->engine->port_max;
	ports = &client->engine->ports[0];
	
	for (i = 0; i < limit; i++) {
		if (ports[i].in_use &&
		    strcmp (ports[i].name, port_name) == 0) {
			port = jack_port_new (client, ports[i].id,
					      client->engine);
			return jack_port_request_monitor (port, onoff);
			free (port);
			return 0;
		}
	}

	return -1;
}

int
jack_port_ensure_monitor (jack_port_t *port, int yn)
{
	if (yn) {
		if (port->shared->monitor_requests == 0) {
			port->shared->monitor_requests++;
		}
	} else {
		if (port->shared->monitor_requests > 0) {
			port->shared->monitor_requests = 0;
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

jack_uuid_t
jack_port_uuid (const jack_port_t *port)
{
	return port->shared->uuid;
}

int
jack_port_get_aliases (const jack_port_t *port, char* const aliases[2])
{
	int cnt = 0;
	
	if (port->shared->alias1[0] != '\0') {
		snprintf (aliases[0], JACK_CLIENT_NAME_SIZE+JACK_PORT_NAME_SIZE, "%s", port->shared->alias1);
		cnt++;
	}

	if (port->shared->alias2[0] != '\0') {
		snprintf (aliases[1], JACK_CLIENT_NAME_SIZE+JACK_PORT_NAME_SIZE, "%s", port->shared->alias2);
		cnt++;
	}

	return cnt;
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
	return jack_uuid_compare (port->shared->client_id, client->control->uuid) == 0;
}

int
jack_port_flags (const jack_port_t *port)
{
	return port->shared->flags;
}

const char *
jack_port_type (const jack_port_t *port)
{
	return port->type_info->type_name;
}

int
jack_port_set_name (jack_port_t *port, const char *new_name)
{
	char *colon;
	int len;

	colon = strchr (port->shared->name, ':');
	len = sizeof (port->shared->name) -
		((int) (colon - port->shared->name)) - 2;
	snprintf (colon+1, len, "%s", new_name);
        
	return 0;
}

int
jack_port_set_alias (jack_port_t *port, const char *alias)
{
	if (port->shared->alias1[0] == '\0') {
		snprintf (port->shared->alias1, sizeof (port->shared->alias1), "%s", alias);
	} else if (port->shared->alias2[0] == '\0') {
		snprintf (port->shared->alias2, sizeof (port->shared->alias2), "%s", alias);
	} else {
		return -1;
	}

	return 0;
}

int
jack_port_unset_alias (jack_port_t *port, const char *alias)
{
	if (strcmp (port->shared->alias1, alias) == 0) {
		port->shared->alias1[0] = '\0';
	} else if (strcmp (port->shared->alias2, alias) == 0) {
		port->shared->alias2[0] = '\0';
	} else {
		return -1;
	}

	return 0;
}

void 
jack_port_set_latency_range (jack_port_t *port, jack_latency_callback_mode_t mode, jack_latency_range_t *range)
{
	if (mode == JackCaptureLatency) {
		port->shared->capture_latency = *range;

		/* hack to set port->shared->latency up for 
		 * backend ports
		 */
		if ((port->shared->flags & JackPortIsOutput) && (port->shared->flags & JackPortIsPhysical))
			port->shared->latency = (range->min + range->max) / 2;
	} else {
		port->shared->playback_latency = *range;

		/* hack to set port->shared->latency up for 
		 * backend ports
		 */
		if ((port->shared->flags & JackPortIsInput) && (port->shared->flags & JackPortIsPhysical))
			port->shared->latency = (range->min + range->max) / 2;

	}
}

void 
jack_port_get_latency_range (jack_port_t *port, jack_latency_callback_mode_t mode, jack_latency_range_t *range)
{
	if (mode == JackCaptureLatency)
		*range = port->shared->capture_latency;
	else
		*range = port->shared->playback_latency;
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

static void 
jack_audio_port_mixdown (jack_port_t *port, jack_nframes_t nframes)
{
	JSList *node;
	jack_port_t *input;
#ifndef ARCH_X86
	jack_nframes_t n;
	jack_default_audio_sample_t *dst, *src;
#endif
	jack_default_audio_sample_t *buffer;

	/* by the time we've called this, we've already established
	   the existence of more than one connection to this input
	   port and allocated a mix_buffer.
	*/

	/* no need to take connection lock, since this is called
	   from the process() callback, and the jack server
	   ensures that no changes to connections happen
	   during this time.
	*/

	node = port->connections;
	input = (jack_port_t *) node->data;
	buffer = port->mix_buffer;

#ifndef USE_DYNSIMD
	memcpy (buffer, jack_output_port_buffer (input),
		sizeof (jack_default_audio_sample_t) * nframes);
#else /* USE_DYNSIMD */
	opt_copy (buffer, jack_output_port_buffer (input), nframes);
#endif /* USE_DYNSIMD */

	for (node = jack_slist_next (node); node;
	     node = jack_slist_next (node)) {

		input = (jack_port_t *) node->data;

#ifndef USE_DYNSIMD
		n = nframes;
		dst = buffer;
		src = jack_output_port_buffer (input);

		while (n--) {
			*dst++ += *src++;
		}
#else /* USE_DYNSIMD */
		opt_mix (buffer, jack_output_port_buffer (input), nframes);
#endif /* USE_DYNSIMD */
	}
}





