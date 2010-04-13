/* -*- Mode: C ; c-basic-offset: 2 -*- */
/*
 * ALSA SEQ < - > JACK MIDI bridge
 *
 * Copyright (c) 2006,2007 Dmitry S. Baikov <c0ff@konstruktiv.org>
 * Copyright (c) 2007,2008,2009 Nedko Arnaudov <nedko@arnaudov.name>
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
#include <semaphore.h>
#include <alsa/asoundlib.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>

#include "list.h"
#include "a2j.h"
#include "port.h"
#include "port_hash.h"
#include "port_thread.h"

struct a2j_port *
a2j_find_port_by_addr(
	struct a2j_stream * stream_ptr,
	snd_seq_addr_t addr)
{
	struct list_head * node_ptr;
	struct a2j_port * port_ptr;

	list_for_each(node_ptr, &stream_ptr->list)
	{
		port_ptr = list_entry(node_ptr, struct a2j_port, siblings);
		if (port_ptr->remote.client == addr.client && port_ptr->remote.port == addr.port)
		{
			return port_ptr;
		}
	}

	return NULL;
}

struct a2j_port *
a2j_find_port_by_jack_port_name(
	struct a2j_stream * stream_ptr,
	const char * jack_port)
{
	struct list_head * node_ptr;
	struct a2j_port * port_ptr;

	list_for_each(node_ptr, &stream_ptr->list)
	{
		port_ptr = list_entry(node_ptr, struct a2j_port, siblings);
		if (strcmp(port_ptr->name, jack_port) == 0)
		{
			return port_ptr;
		}
	}

	return NULL;
}

/*
 * ==================== Port add/del handling thread ==============================
 */

static
void
a2j_update_port_type (struct a2j * self, snd_seq_addr_t addr, int caps, const snd_seq_port_info_t * info)
{
	struct a2j_stream * stream_ptr;
	int alsa_mask;
	struct a2j_port * port_ptr;

	a2j_debug("update_port_type(%d:%d)", addr.client, addr.port);

	stream_ptr = &self->stream;
	port_ptr = a2j_find_port_by_addr(stream_ptr, addr);

	if (self->input) {
		alsa_mask = SND_SEQ_PORT_CAP_SUBS_READ;
	} else {
		alsa_mask = SND_SEQ_PORT_CAP_SUBS_WRITE;
	}

	if (port_ptr != NULL && (caps & alsa_mask) != alsa_mask) {
		a2j_debug("setdead: %s", port_ptr->name);
		port_ptr->is_dead = true;
	}

	if (port_ptr == NULL && (caps & alsa_mask) == alsa_mask) {
		if(jack_ringbuffer_write_space(stream_ptr->new_ports) >= sizeof(port_ptr)) {
			port_ptr = a2j_port_create (self, addr, info);
			if (port_ptr != NULL) {
				jack_ringbuffer_write(stream_ptr->new_ports, (char *)&port_ptr, sizeof(port_ptr));
			}
		} else {
			a2j_error( "dropping new port event... increase MAX_PORTS" );
		}
	}
}

void
a2j_update_port (struct a2j * self, snd_seq_addr_t addr, const snd_seq_port_info_t * info)
{
	unsigned int port_caps = snd_seq_port_info_get_capability(info);
	unsigned int port_type = snd_seq_port_info_get_type(info);

	a2j_debug("port %u:%u", addr.client, addr.port);
	a2j_debug("port type: 0x%08X", port_type);
	a2j_debug("port caps: 0x%08X", port_caps);

	if (port_type & SND_SEQ_PORT_TYPE_SPECIFIC) {
		a2j_debug("SPECIFIC");
	}

	if (port_type & SND_SEQ_PORT_TYPE_MIDI_GENERIC) {
		a2j_debug("MIDI_GENERIC");
	}

	if (port_type & SND_SEQ_PORT_TYPE_MIDI_GM) {
		a2j_debug("MIDI_GM");
	}

	if (port_type & SND_SEQ_PORT_TYPE_MIDI_GS) {
		a2j_debug("MIDI_GS");
	}

	if (port_type & SND_SEQ_PORT_TYPE_MIDI_XG) {
		a2j_debug("MIDI_XG");
	}

	if (port_type & SND_SEQ_PORT_TYPE_MIDI_MT32) {
		a2j_debug("MIDI_MT32");
	}

	if (port_type & SND_SEQ_PORT_TYPE_MIDI_GM2) {
		a2j_debug("MIDI_GM2");
	}

	if (port_type & SND_SEQ_PORT_TYPE_SYNTH) {
		a2j_debug("SYNTH");
	}

	if (port_type & SND_SEQ_PORT_TYPE_DIRECT_SAMPLE) {
		a2j_debug("DIRECT_SAMPLE");
	}

	if (port_type & SND_SEQ_PORT_TYPE_SAMPLE) {
		a2j_debug("SAMPLE");
	}

	if (port_type & SND_SEQ_PORT_TYPE_HARDWARE) {
		a2j_debug("HARDWARE");
	}

	if (port_type & SND_SEQ_PORT_TYPE_SOFTWARE) {
		a2j_debug("SOFTWARE");
	}

	if (port_type & SND_SEQ_PORT_TYPE_SYNTHESIZER) {
		a2j_debug("SYNTHESIZER");
	}

	if (port_type & SND_SEQ_PORT_TYPE_PORT) {
		a2j_debug("PORT");
	}

	if (port_type & SND_SEQ_PORT_TYPE_APPLICATION) {
		a2j_debug("APPLICATION");
	}

	if (port_type == 0) {
		a2j_debug("Ignoring port of type 0");
		return;
	}

	if ((port_type & SND_SEQ_PORT_TYPE_HARDWARE) && self->ignore_hardware_ports) {
		a2j_debug("Ignoring hardware port");
		return;
	}

	if (port_caps & SND_SEQ_PORT_CAP_NO_EXPORT) {
		a2j_debug("Ignoring no-export port");
		return;
	}

	a2j_update_port_type (self, addr, port_caps, info);
}

void
a2j_free_ports (jack_ringbuffer_t * ports)
{
	struct a2j_port *port;
	int sz;
	while ((sz = jack_ringbuffer_read(ports, (char*)&port, sizeof(port)))) {
		assert (sz == sizeof(port));
		a2j_info("port deleted: %s", port->name);
		list_del (&port->siblings);
		a2j_port_free(port);
	}
}

void
a2j_update_ports (struct a2j * self)
{
	snd_seq_addr_t addr;
	int size;

	while ((size = jack_ringbuffer_read(self->port_add, (char *)&addr, sizeof(addr))) != 0) {
		snd_seq_port_info_t * info;
		int err;

		snd_seq_port_info_alloca(&info);

		assert (size == sizeof(addr));
		assert (addr.client != self->client_id);

		if ((err = snd_seq_get_any_port_info(self->seq, addr.client, addr.port, info)) >= 0) {
			a2j_update_port(self, addr, info);
		} else {
			//a2j_port_setdead(self->stream[A2J_PORT_CAPTURE].ports, addr);
			//a2j_port_setdead(self->stream[A2J_PORT_PLAYBACK].ports, addr);
		}
	}
}
