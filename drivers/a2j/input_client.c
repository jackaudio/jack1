/* -*- Mode: C ; c-basic-offset: 2 -*- */
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
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <alsa/asoundlib.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>

#include "list.h"
#include "a2j.h"
#include "port_hash.h"
#include "port.h"
#include "port_thread.h"

static bool g_freewheeling = false;
bool g_keep_walking = true;
bool g_keep_alsa_walking = false;
bool g_stop_request = false;
bool g_started = false;

void
a2j_info (const char* fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	vfprintf (stdout, fmt, ap);
	fputc ('\n', stdout);
}

void
a2j_error (const char* fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	vfprintf (stdout, fmt, ap);
	fputc ('\n', stdout);
}


void
a2j_debug (const char* fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	vfprintf (stderr, fmt, ap);
	fputc ('\n', stdout);
}

void
a2j_warning (const char* fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	vfprintf (stdout, fmt, ap);
	fputc ('\n', stdout);
}

static bool
a2j_stream_init(struct a2j * self)
{
	struct a2j_stream *str = &self->stream;

	str->new_ports = jack_ringbuffer_create (MAX_PORTS * sizeof(struct a2j_port *));
	if (str->new_ports == NULL) {
		return false;
	}
	
	snd_midi_event_new (MAX_EVENT_SIZE, &str->codec);
	INIT_LIST_HEAD (&str->list);

	return true;
}

static void 
a2j_stream_attach (struct a2j_stream * stream_ptr)
{
}

static void
a2j_stream_detach (struct a2j_stream * stream_ptr)
{
	struct a2j_port * port_ptr;
	struct list_head * node_ptr;

	while (!list_empty (&stream_ptr->list)) {
		node_ptr = stream_ptr->list.next;
		list_del (node_ptr);
		port_ptr = list_entry (node_ptr, struct a2j_port, siblings);
		a2j_info ("port deleted: %s", port_ptr->name);
		a2j_port_free (port_ptr);
	}
}

static
void
a2j_stream_close (struct a2j * self)
{
	struct a2j_stream *str = &self->stream;

	if (str->codec)
		snd_midi_event_free (str->codec);
	if (str->new_ports)
		jack_ringbuffer_free (str->new_ports);
}



/*
 * =================== Input/output port handling =========================
 */

void a2j_add_ports (struct a2j_stream * str)
{
	struct a2j_port * port_ptr;
	while (jack_ringbuffer_read (str->new_ports, (char *)&port_ptr, sizeof(port_ptr))) {
		a2j_debug("jack: inserted port %s", port_ptr->name);
		a2j_port_insert (str->port_hash, port_ptr);
	}
}

static
void
a2j_port_event (struct a2j * self, snd_seq_event_t * ev)
{
	const snd_seq_addr_t addr = ev->data.addr;

	if (addr.client == self->client_id)
		return;

	if (ev->type == SND_SEQ_EVENT_PORT_START || ev->type == SND_SEQ_EVENT_PORT_CHANGE) {
		if (jack_ringbuffer_write_space(self->port_add) >= sizeof(addr)) {
			a2j_debug("port_event: add/change %d:%d", addr.client, addr.port);
			jack_ringbuffer_write(self->port_add, (char*)&addr, sizeof(addr));
		} else {
			a2j_error("dropping port_event: add/change %d:%d", addr.client, addr.port);
		}
	} else if (ev->type == SND_SEQ_EVENT_PORT_EXIT) {
		a2j_debug("port_event: del %d:%d", addr.client, addr.port);
		a2j_port_setdead(self->stream.port_hash, addr);
	}
}

static void
a2j_input_event (struct a2j * self, snd_seq_event_t * alsa_event)
{
	jack_midi_data_t data[MAX_EVENT_SIZE];
	struct a2j_stream *str = &self->stream;
	long size;
	struct a2j_port *port;
	jack_nframes_t now;

	now = jack_frame_time (self->jack_client);
  
	if ((port = a2j_port_get(str->port_hash, alsa_event->source)) == NULL) {
		return;
	}

	/*
	 * RPNs, NRPNs, Bank Change, etc. need special handling
	 * but seems, ALSA does it for us already.
	 */
	snd_midi_event_reset_decode(str->codec);
	if ((size = snd_midi_event_decode(str->codec, data, sizeof(data), alsa_event))<0) {
		return;
	}

	// fixup NoteOn with vel 0
	if ((data[0] & 0xF0) == 0x90 && data[2] == 0x00) {
		data[0] = 0x80 + (data[0] & 0x0F);
		data[2] = 0x40;
	}

	a2j_debug("input: %d bytes at event_frame=%u", (int)size, now);

	if (jack_ringbuffer_write_space(port->inbound_events) >= (sizeof(struct a2j_alsa_midi_event) + size)) {
		struct a2j_alsa_midi_event ev;
		char *ev_charp = (char*) &ev;
		size_t limit;
		size_t to_write = sizeof(ev);

		jack_ringbuffer_data_t vec[2];
		jack_ringbuffer_get_write_vector( port->inbound_events, vec );
		ev.time = now;
		ev.size = size;

    
		limit = (to_write > vec[0].len ? vec[0].len : to_write);
		if( limit ) {
			memcpy( vec[0].buf, ev_charp, limit );
			to_write -= limit;
			ev_charp += limit;
			vec[0].buf += limit;
			vec[0].len -= limit;
		}
		if( to_write ) {
			memcpy( vec[1].buf, ev_charp, to_write );
			vec[1].buf += to_write;
			vec[1].len -= to_write;
		}

		to_write = size;
		ev_charp = (char *)data;
		limit = (to_write > vec[0].len ? vec[0].len : to_write);
		if( limit )
			memcpy( vec[0].buf, ev_charp, limit );
		to_write -= limit;
		ev_charp += limit;
		if( to_write )
			memcpy( vec[1].buf, ev_charp, to_write );

		jack_ringbuffer_write_advance( port->inbound_events, sizeof(ev) + size );
	} else {
		a2j_error ("MIDI data lost (incoming event buffer full): %ld bytes lost", size);
	}

}


/* ALSA */

void* alsa_input_thread(void * arg)
{
	struct a2j * self = arg;
	int npfd;
	struct pollfd * pfd;
	snd_seq_addr_t addr;
	snd_seq_client_info_t * client_info;
	snd_seq_port_info_t * port_info;
	bool initial;
	snd_seq_event_t * event;
	int ret;

	npfd = snd_seq_poll_descriptors_count(self->seq, POLLIN);
	pfd = (struct pollfd *)alloca(npfd * sizeof(struct pollfd));
	snd_seq_poll_descriptors(self->seq, pfd, npfd, POLLIN);

	initial = true;
	while (g_keep_alsa_walking) {
		if ((ret = poll(pfd, npfd, 1000)) > 0) {

			while (snd_seq_event_input (self->seq, &event) > 0) {
				if (initial) {
					snd_seq_client_info_alloca(&client_info);
					snd_seq_port_info_alloca(&port_info);
					snd_seq_client_info_set_client(client_info, -1);
					while (snd_seq_query_next_client(self->seq, client_info) >= 0) {
						addr.client = snd_seq_client_info_get_client(client_info);
						if (addr.client == SND_SEQ_CLIENT_SYSTEM || addr.client == self->client_id) {
							continue;
						}
						snd_seq_port_info_set_client(port_info, addr.client);
						snd_seq_port_info_set_port(port_info, -1);
						while (snd_seq_query_next_port(self->seq, port_info) >= 0) {
							addr.port = snd_seq_port_info_get_port(port_info);
							a2j_update_port(self, addr, port_info);
						}
					}

					initial = false;
				}

				if (event->source.client == SND_SEQ_CLIENT_SYSTEM) {
					a2j_port_event(self, event);
				} else {
					a2j_input_event(self, event);
				}

				snd_seq_free_event (event);
			}
		}
	}

	return (void*) 0;
}


/* JACK */

static int
a2j_process (jack_nframes_t nframes, void * arg)
{
	struct a2j* self = (struct a2j *) arg;
	struct a2j_stream * stream_ptr;
	int i;
	struct a2j_port ** port_ptr;
	struct a2j_port * port;

	if (g_freewheeling) {
		return 0;
	}

	self->cycle_start = jack_last_frame_time (self->jack_client);
	
	stream_ptr = &self->stream;
	a2j_add_ports (stream_ptr);
	
	// process ports
	
	for (i = 0 ; i < PORT_HASH_SIZE ; i++) {

		port_ptr = &stream_ptr->port_hash[i];

		while (*port_ptr != NULL) {

			struct a2j_alsa_midi_event ev;
			jack_nframes_t now;
			jack_nframes_t one_period;
			char *ev_buf;
				
			port = *port_ptr;
			
			if (port->is_dead) {
				if (jack_ringbuffer_write_space (self->port_del) >= sizeof(port_ptr)) {
				
					a2j_debug("jack: removed port %s", port->name);
					*port_ptr = port->next;
					jack_ringbuffer_write (self->port_del, (char*)&port, sizeof(port));
				} else {
					a2j_error ("port deletion lost - no space in event buffer!");
				}

				port_ptr = &port->next;
				continue;
			}

			port->jack_buf = jack_port_get_buffer(port->jack_port, nframes);
				
			/* grab data queued by the ALSA input thread and write it into the JACK
			   port buffer. it will delivered during the JACK period that this
			   function is called from.
			*/
				
			/* first clear the JACK port buffer in preparation for new data 
			 */
				
			// a2j_debug ("PORT: %s process input", jack_port_name (port->jack_port));
				
			jack_midi_clear_buffer (port->jack_buf);
				
			now = jack_frame_time (self->jack_client);
			one_period = jack_get_buffer_size (self->jack_client);
				
			while (jack_ringbuffer_peek (port->inbound_events, (char*)&ev, sizeof(ev) ) == sizeof(ev) ) {
					
				jack_midi_data_t* buf;
				jack_nframes_t offset;
					
				if (ev.time >= self->cycle_start) {
					break;
				}
					
				//jack_ringbuffer_read_advance (port->inbound_events, sizeof (ev));
				ev_buf = (char *) alloca( sizeof(ev) + ev.size );
					
				if (jack_ringbuffer_peek (port->inbound_events, ev_buf, sizeof(ev) + ev.size ) != sizeof(ev) + ev.size)
					break;
					
				offset = self->cycle_start - ev.time;
				if (offset > one_period) {
					/* from a previous cycle, somehow. cram it in at the front */
					offset = 0;
				} else {
					/* offset from start of the current cycle */
					offset = one_period - offset;
				}
					
				a2j_debug ("event at %d offset %d", ev.time, offset);
					
				/* make sure there is space for it */
					
				buf = jack_midi_event_reserve (port->jack_buf, offset, ev.size);
					
				if (buf) {
					/* grab the event */
					memcpy( buf, ev_buf + sizeof(ev), ev.size );
				} else {
					/* throw it away (no space) */
					a2j_error ("threw away MIDI event - not reserved at time %d", ev.time);
				}
				jack_ringbuffer_read_advance (port->inbound_events, sizeof(ev) + ev.size);
					
				a2j_debug("input on %s: sucked %d bytes from inbound at %d", jack_port_name (port->jack_port), ev.size, ev.time);
			}
			
			port_ptr = &port->next;
		}
	}

	return 0;
}

static
void
a2j_freewheel(
	int starting,
	void * arg)
{
	g_freewheeling = starting;
}

static
void
a2j_shutdown(
	void * arg)
{
	a2j_warning("JACK server shutdown notification received.");
	g_stop_request = true;
}

int
connect_to_alsa (struct a2j* self)
{
	int error;
	void * thread_status;

	self->port_add = jack_ringbuffer_create(2 * MAX_PORTS * sizeof(snd_seq_addr_t));
	if (self->port_add == NULL) {
		goto free_self;
	}

	self->port_del = jack_ringbuffer_create(2 * MAX_PORTS * sizeof(struct a2j_port *));
	if (self->port_del == NULL) {
		goto free_ringbuffer_add;
	}

	if (!a2j_stream_init(self)) {
		goto free_ringbuffer_outbound;
	}

	if ((error = snd_seq_open(&self->seq, "hw", SND_SEQ_OPEN_DUPLEX, 0)) < 0) {
		a2j_error("failed to open alsa seq");
		goto close_stream;
	}

	if ((error = snd_seq_set_client_name(self->seq, "midi_in")) < 0) {
		a2j_error("snd_seq_set_client_name() failed");
		goto close_seq_client;
	}

	if ((self->port_id = snd_seq_create_simple_port(
		self->seq,
		"port",
		SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_WRITE
#ifndef DEBUG
		|SND_SEQ_PORT_CAP_NO_EXPORT
#endif
		,SND_SEQ_PORT_TYPE_APPLICATION)) < 0) {

		a2j_error("snd_seq_create_simple_port() failed");
		goto close_seq_client;
	}

	if ((self->client_id = snd_seq_client_id(self->seq)) < 0) {
		a2j_error("snd_seq_client_id() failed");
		goto close_seq_client;
	}
	
	if ((self->queue = snd_seq_alloc_queue(self->seq)) < 0) {
		a2j_error("snd_seq_alloc_queue() failed");
		goto close_seq_client;
	}

	snd_seq_start_queue (self->seq, self->queue, 0); 

	a2j_stream_attach (&self->stream);

	if ((error = snd_seq_nonblock(self->seq, 1)) < 0) {
		a2j_error("snd_seq_nonblock() failed");
		goto close_seq_client;
	}

	snd_seq_drop_input (self->seq);

	a2j_add_ports(&self->stream);

	if (sem_init(&self->io_semaphore, 0, 0) < 0) {
		a2j_error("can't create IO semaphore");
		goto close_jack_client;
	}

	g_keep_alsa_walking = true;

	if (pthread_create(&self->alsa_io_thread, NULL, alsa_input_thread, self) < 0)
	{
		a2j_error("cannot start ALSA input thread");
		goto sem_destroy;
	}

	/* wake the poll loop in the alsa input thread so initial ports are fetched */
	if ((error = snd_seq_connect_from (self->seq, self->port_id, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE)) < 0) {
		a2j_error("snd_seq_connect_from() failed");
		goto join_io_thread;
	}

	return 0;

	g_keep_alsa_walking = false;  /* tell alsa threads to stop */
	snd_seq_disconnect_from(self->seq, self->port_id, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
  join_io_thread:
	pthread_join(self->alsa_io_thread, &thread_status);
  sem_destroy:
	sem_destroy(&self->io_semaphore);
  close_jack_client:
	if ((error = jack_client_close(self->jack_client)) < 0) {
		a2j_error("Cannot close jack client");
	}
  close_seq_client:
	snd_seq_close(self->seq);
  close_stream:
	a2j_stream_close(self);
  free_ringbuffer_outbound:
	jack_ringbuffer_free(self->outbound_events);
	jack_ringbuffer_free(self->port_del);
  free_ringbuffer_add:
	jack_ringbuffer_free(self->port_add);
  free_self:
	free(self);
	return -1;
}

/* JACK internal client API: 2 entry points 
 */

int
jack_initialize (jack_client_t *client, const char* load_init)
{
	struct a2j* self = calloc(1, sizeof(struct a2j));

	if (!self) {
		return -1;
	}

	self->jack_client = client;

	self->input = 1;
	self->ignore_hardware_ports = 0;
        self->finishing = 0;

	if (load_init) {
		char* args = strdup (load_init);
		char* token;
		char* ptr = args;
		char* savep;

		while (1) {
			if ((token = strtok_r (ptr, ", ", &savep)) == NULL) {
				break;
			}

			if (strncasecmp (token, "in", 2) == 0) {
				self->input = 1;
			}

			if (strncasecmp (token, "out", 2) == 0) {
				self->input = 0;
			}

			if (strncasecmp (token, "hw", 2) == 0) {
				self->ignore_hardware_ports = 0;
			}
			
			ptr = NULL;
		}

		free (args);
	}

	if (connect_to_alsa (self)) {
		free (self);
		return -1;
	}

	jack_set_process_callback (client, a2j_process, self);
	jack_set_freewheel_callback (client, a2j_freewheel, NULL);
	jack_on_shutdown (client, a2j_shutdown, NULL);

	jack_activate (client);

	return 0;
}

void
jack_finish (void *arg)
{
	struct a2j* self = (struct a2j*) arg;
	void* thread_status;

        self->finishing = 1;
        
	a2j_debug("midi: delete");
	
	g_keep_alsa_walking = false;  /* tell alsa io thread to stop, whenever they wake up */
	/* do something that we need to do anyway and will wake the io thread, then join */
	snd_seq_disconnect_from (self->seq, self->port_id, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
	a2j_debug ("wait for ALSA io thread\n");
	pthread_join (self->alsa_io_thread, &thread_status);
	a2j_debug ("thread done\n");
	
	jack_ringbuffer_reset (self->port_add);
	
	a2j_stream_detach (&self->stream);
	
	snd_seq_close(self->seq);
	self->seq = NULL;
	
	a2j_stream_close (self);
	
	jack_ringbuffer_free(self->port_add);
	jack_ringbuffer_free(self->port_del);
	
	free (self);
}
