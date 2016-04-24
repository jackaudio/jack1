/*
 * ALSA SEQ < - > JACK MIDI bridge
 *
 * Copyright (c) 2006,2007 Dmitry S. Baikov <c0ff@konstruktiv.org>
 * Copyright (c) 2007,2008,2009 Nedko Arnaudov <nedko@arnaudov.name>
 * Copyright (c) 2009,2010 Paul Davis <paul@linuxaudiosystems.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <stdbool.h>
#include <ctype.h>
#include <semaphore.h>
#include <alsa/asoundlib.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>

#include "list.h"
#include "a2j.h"
#include "port_hash.h"
#include "port.h"

/* This should be part of JACK API */
#define JACK_IS_VALID_PORT_NAME_CHAR(c)		\
	(isalnum (c) ||				 \
	 (c) == '/' ||				\
	 (c) == '_' ||				\
	 (c) == '(' ||				\
	 (c) == ')' ||				\
	 (c) == '-' ||				\
	 (c) == '[' ||				\
	 (c) == ']')

static
int
a2j_alsa_connect_from (alsa_midi_driver_t * driver, int client, int port)
{
	snd_seq_port_subscribe_t* sub;
	snd_seq_addr_t seq_addr;
	int err;

	snd_seq_port_subscribe_alloca (&sub);
	seq_addr.client = client;
	seq_addr.port = port;
	snd_seq_port_subscribe_set_sender (sub, &seq_addr);
	seq_addr.client = driver->client_id;
	seq_addr.port = driver->port_id;
	snd_seq_port_subscribe_set_dest (sub, &seq_addr);

	snd_seq_port_subscribe_set_time_update (sub, 1);
	snd_seq_port_subscribe_set_queue (sub, driver->queue);
	snd_seq_port_subscribe_set_time_real (sub, 1);

	if ((err = snd_seq_subscribe_port (driver->seq, sub))) {
		a2j_error ("can't subscribe to %d:%d - %s", client, port, snd_strerror (err));
	}

	return err;
}

void
a2j_port_setdead (a2j_port_hash_t hash, snd_seq_addr_t addr)
{
	struct a2j_port *port = a2j_port_get (hash, addr);

	if (port) {
		port->is_dead = true; // see jack_process_internal
	} else {
		a2j_debug ("port_setdead: not found (%d:%d)", addr.client, addr.port);
	}
}

void
a2j_port_free (struct a2j_port * port)
{
	// snd_seq_disconnect_from (driver->seq, driver->port_id, port->remote.client, port->remote.port);
	// snd_seq_disconnect_to (driver->seq, driver->port_id, port->remote.client, port->remote.port);

	if (port->inbound_events) {
		jack_ringbuffer_free (port->inbound_events);
	}

	if (port->jack_port != JACK_INVALID_PORT && !port->driver_ptr->finishing) {
		jack_port_unregister (port->driver_ptr->jack_client, port->jack_port);
	}

	free (port);
}

void
a2j_port_fill_name (struct a2j_port * port_ptr, int dir, snd_seq_client_info_t * client_info_ptr,
		    const snd_seq_port_info_t * port_info_ptr, bool make_unique)
{
	char *c;
	const char* const client_name = snd_seq_client_info_get_name (client_info_ptr);
	const char* const port_name = snd_seq_port_info_get_name (port_info_ptr);

	if (make_unique) {
		if (strstr (port_name, client_name) == port_name) {
			/* entire client name is part of the port name so don't replicate it */
			snprintf (port_ptr->name,
			          sizeof(port_ptr->name),
			          "[%d:%d] %s (%s)",
			          snd_seq_client_info_get_client (client_info_ptr),
			          snd_seq_port_info_get_port (port_info_ptr),
			          port_name,
			  (dir == A2J_PORT_CAPTURE ? "out" : "in"));
		} else {
			snprintf (port_ptr->name,
			          sizeof(port_ptr->name),
			          "[%d:%d] %s %s (%s)",
			          snd_seq_client_info_get_client (client_info_ptr),
			          snd_seq_port_info_get_port (port_info_ptr),
			          client_name,
			          port_name,
			          (dir == A2J_PORT_CAPTURE ? "out" : "in"));
		}
	} else {
		if (strstr (port_name, client_name) == port_name) {
			/* entire client name is part of the port name so don't replicate it */
			snprintf (port_ptr->name,
			          sizeof(port_ptr->name),
			          "%s (%s)",
			          port_name,
			          (dir == A2J_PORT_CAPTURE ? "out" : "in"));
		} else {
			snprintf (port_ptr->name,
			          sizeof(port_ptr->name),
			          "%s %s (%s)",
			          client_name,
			          snd_seq_port_info_get_name (port_info_ptr),
			          (dir == A2J_PORT_CAPTURE ? "out" : "in"));
		}
	}

	// replace all offending characters with ' '
	for (c = port_ptr->name; *c; ++c) {
		if (!JACK_IS_VALID_PORT_NAME_CHAR (*c)) {
			*c = ' ';
		}
	}
}

struct a2j_port *
a2j_port_create (alsa_midi_driver_t * driver, int dir, snd_seq_addr_t addr, const snd_seq_port_info_t * info)
{
	struct a2j_port *port;
	int err;
	int client;
	snd_seq_client_info_t * client_info_ptr;
	int jack_caps;
	struct a2j_stream * stream_ptr;

	stream_ptr = &driver->stream[dir];

	if ((err = snd_seq_client_info_malloc (&client_info_ptr)) != 0) {
		a2j_error ("Failed to allocate client info");
		goto fail;
	}

	client = snd_seq_port_info_get_client (info);

	err = snd_seq_get_any_client_info (driver->seq, client, client_info_ptr);
	if (err != 0) {
		a2j_error ("Failed to get client info");
		goto fail_free_client_info;
	}

	a2j_debug ("client name: '%s'", snd_seq_client_info_get_name (client_info_ptr));
	a2j_debug ("port name: '%s'", snd_seq_port_info_get_name (info));

	port = calloc (1, sizeof(struct a2j_port));
	if (!port) {
		goto fail_free_client_info;
	}

	port->driver_ptr = driver;
	port->jack_port = JACK_INVALID_PORT;
	port->remote = addr;

	a2j_port_fill_name (port, dir, client_info_ptr, info, false);

	/* Add port to list early, before registering to JACK, so map functionality is guaranteed to work during port registration */
	list_add_tail (&port->siblings, &stream_ptr->list);

	if (dir == A2J_PORT_CAPTURE) {
		jack_caps = JackPortIsOutput;
	} else {
		jack_caps = JackPortIsInput;
	}

	/* mark anything that looks like a hardware port as physical&terminal */
	if (snd_seq_port_info_get_type (info) & (SND_SEQ_PORT_TYPE_HARDWARE | SND_SEQ_PORT_TYPE_PORT | SND_SEQ_PORT_TYPE_SPECIFIC)) {
		jack_caps |= JackPortIsPhysical | JackPortIsTerminal;
	}

	port->jack_port = jack_port_register (driver->jack_client, port->name, JACK_DEFAULT_MIDI_TYPE, jack_caps, 0);
	if (port->jack_port == JACK_INVALID_PORT) {
		a2j_error ("jack_port_register() failed for '%s'", port->name);
		goto fail_free_port;
	}

	if (dir == A2J_PORT_CAPTURE) {
		err = a2j_alsa_connect_from (driver, port->remote.client, port->remote.port);
	} else {
		err = snd_seq_connect_to (driver->seq, driver->port_id, port->remote.client, port->remote.port);
	}

	if (err) {
		a2j_debug ("port skipped: %s", port->name);
		goto fail_free_port;
	}

	port->inbound_events = jack_ringbuffer_create (MAX_EVENT_SIZE * 16);

	a2j_debug ("port created: %s", port->name);
	return port;

fail_free_port:
	list_del (&port->siblings);

	a2j_port_free (port);

fail_free_client_info:
	snd_seq_client_info_free (client_info_ptr);

fail:
	return NULL;
}
