/* -*- Mode: C ; c-basic-offset: 2 -*- */
/*
 * ALSA SEQ < - > JACK MIDI bridge
 *
 * Copyright (c) 2006,2007 Dmitry S. Baikov <c0ff@konstruktiv.org>
 * Copyright (c) 2007,2008,2009 Nedko Arnaudov <nedko@arnaudov.name>
 * Copyright (c) 2009,2010,2013 Paul Davis <paul@linuxaudiosystems.com>
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

#ifdef A2J_DEBUG
bool a2j_do_debug = false;

void
_a2j_debug (const char* fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	vfprintf (stderr, fmt, ap);
	fputc ('\n', stderr);
}
#endif

void
a2j_error (const char* fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	vfprintf (stdout, fmt, ap);
	fputc ('\n', stdout);
}

static bool
a2j_stream_init(alsa_midi_driver_t* driver, int which)
{
  struct a2j_stream *str = &driver->stream[which];
  
  str->new_ports = jack_ringbuffer_create (MAX_PORTS * sizeof(struct a2j_port *));
  if (str->new_ports == NULL) {
    return false;
  }
  
  snd_midi_event_new (MAX_EVENT_SIZE, &str->codec);
  INIT_LIST_HEAD (&str->list);
  
  return true;
}

static void
a2j_stream_detach (struct a2j_stream * stream_ptr)
{
	struct a2j_port * port_ptr;
	struct list_head * node_ptr;
        
        if (!stream_ptr) {
          return;
        }

	while (!list_empty (&stream_ptr->list)) {
		node_ptr = stream_ptr->list.next;
		list_del (node_ptr);
		port_ptr = list_entry (node_ptr, struct a2j_port, siblings);
		a2j_debug ("port deleted: %s", port_ptr->name);
		a2j_port_free (port_ptr);
	}
}

static
void
a2j_stream_close (alsa_midi_driver_t* driver, int which)
{
	struct a2j_stream *str = &driver->stream[which];

        if (!str) {
          return;
        }

	if (str->codec)
		snd_midi_event_free (str->codec);
	if (str->new_ports)
		jack_ringbuffer_free (str->new_ports);
}

static void
stop_threads (alsa_midi_driver_t* driver)
{
  if (driver->running) {
    void* thread_status;
    
    driver->running = false;  /* tell alsa io thread to stop, whenever they wake up */
    /* do something that we need to do anyway and will wake the io thread, then join */
    snd_seq_disconnect_from (driver->seq, driver->port_id, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
    a2j_debug ("wait for ALSA input thread\n");
    pthread_join (driver->alsa_input_thread, &thread_status);
    a2j_debug ("input thread done\n");
    
    /* wake output thread and join */
    sem_post (&driver->output_semaphore);
    pthread_join (driver->alsa_output_thread, &thread_status);
    a2j_debug ("output thread done\n");
  }
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
a2j_port_event (alsa_midi_driver_t* driver, snd_seq_event_t * ev)
{
	const snd_seq_addr_t addr = ev->data.addr;

	if (addr.client == driver->client_id)
		return;

	if (ev->type == SND_SEQ_EVENT_PORT_START) {
		a2j_debug("port_event: add %d:%d", addr.client, addr.port);
		a2j_new_ports (driver, addr);
	} else if (ev->type == SND_SEQ_EVENT_PORT_CHANGE) {
		a2j_debug("port_event: change %d:%d", addr.client, addr.port);
		a2j_update_ports (driver, addr);
	} else if (ev->type == SND_SEQ_EVENT_PORT_EXIT) {
		a2j_debug("port_event: del %d:%d", addr.client, addr.port);
		a2j_port_setdead(driver->stream[A2J_PORT_CAPTURE].port_hash, addr);
		a2j_port_setdead(driver->stream[A2J_PORT_PLAYBACK].port_hash, addr);
	}
}

/* --- INBOUND FROM ALSA TO JACK ---- */

static void
a2j_input_event (alsa_midi_driver_t* driver, snd_seq_event_t * alsa_event)
{
	jack_midi_data_t data[MAX_EVENT_SIZE];
	struct a2j_stream *str = &driver->stream[A2J_PORT_CAPTURE];
	long size;
	struct a2j_port *port;
	jack_nframes_t now;

	now = jack_frame_time (driver->jack_client);
  
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
		if (limit) {
                  memcpy( vec[0].buf, ev_charp, limit );
                  to_write -= limit;
                  ev_charp += limit;
                  vec[0].buf += limit;
                  vec[0].len -= limit;
		}
		if (to_write) {
                  memcpy( vec[1].buf, ev_charp, to_write );
                  vec[1].buf += to_write;
                  vec[1].len -= to_write;
		}

		to_write = size;
		ev_charp = (char *)data;
		limit = (to_write > vec[0].len ? vec[0].len : to_write);
		if (limit) {
			memcpy (vec[0].buf, ev_charp, limit);
                }
		to_write -= limit;
		ev_charp += limit;
		if (to_write) {
			memcpy (vec[1].buf, ev_charp, to_write);
                }

		jack_ringbuffer_write_advance( port->inbound_events, sizeof(ev) + size );
	} else {
		a2j_error ("MIDI data lost (incoming event buffer full): %ld bytes lost", size);
	}

}

static int
a2j_process_incoming (alsa_midi_driver_t* driver, struct a2j_port* port, jack_nframes_t nframes)
{
  jack_nframes_t one_period;
  struct a2j_alsa_midi_event ev;
  char *ev_buf;

  /* grab data queued by the ALSA input thread and write it into the JACK
     port buffer. it will delivered during the JACK period that this
     function is called from.
  */
  
  /* first clear the JACK port buffer in preparation for new data 
   */
  
  a2j_debug ("PORT: %s process input", jack_port_name (port->jack_port));
  
  jack_midi_clear_buffer (port->jack_buf);
  
  one_period = jack_get_buffer_size (driver->jack_client);
  
  while (jack_ringbuffer_peek (port->inbound_events, (char*)&ev, sizeof(ev) ) == sizeof(ev) ) {
    
    jack_midi_data_t* buf;
    jack_nframes_t offset;
    
    a2j_debug ("Seen inbound event from read callback\n");
    
    if (ev.time >= driver->cycle_start) {
      a2j_debug ("event is too late\n");
      break;
    }
    
    //jack_ringbuffer_read_advance (port->inbound_events, sizeof (ev));
    ev_buf = (char *) alloca( sizeof(ev) + ev.size );
    
    if (jack_ringbuffer_peek (port->inbound_events, ev_buf, sizeof(ev) + ev.size ) != sizeof(ev) + ev.size) {
            break;
    }
    
    offset = driver->cycle_start - ev.time;
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
  
  return 0;
}

void* 
alsa_input_thread (void* arg)
{
	alsa_midi_driver_t* driver = arg;
	int npfd;
	struct pollfd * pfd;
	snd_seq_addr_t addr;
	snd_seq_client_info_t * client_info;
	bool initial;
	snd_seq_event_t * event;
	int ret;

	npfd = snd_seq_poll_descriptors_count(driver->seq, POLLIN);
	pfd = (struct pollfd *)alloca(npfd * sizeof(struct pollfd));
	snd_seq_poll_descriptors(driver->seq, pfd, npfd, POLLIN);

	initial = true;

	while (driver->running) {
		if ((ret = poll(pfd, npfd, 1000)) > 0) {

			while (snd_seq_event_input (driver->seq, &event) > 0) {
				if (initial) {
					snd_seq_client_info_alloca(&client_info);
					snd_seq_client_info_set_client(client_info, -1);
					while (snd_seq_query_next_client(driver->seq, client_info) >= 0) {
						addr.client = snd_seq_client_info_get_client(client_info);
						if (addr.client == SND_SEQ_CLIENT_SYSTEM || addr.client == driver->client_id) {
							continue;
						}
						
						a2j_new_ports (driver, addr);
					}

					initial = false;
				}

				if (event->source.client == SND_SEQ_CLIENT_SYSTEM) {
					a2j_port_event(driver, event);
				} else {
					a2j_input_event(driver, event);
				}

				snd_seq_free_event (event);
			}
		}
	}

	return (void*) 0;
}

/* --- OUTBOUND FROM JACK TO ALSA ---- */

int
a2j_process_outgoing (
  alsa_midi_driver_t* driver,
  struct a2j_port * port)
{
  /* collect data from JACK port buffer and queue it for delivery by ALSA output thread */
  
  int nevents;
  jack_ringbuffer_data_t vec[2];
  int i;
  int written = 0;
  size_t limit;
  struct a2j_delivery_event* dev;
  size_t gap = 0;

  jack_ringbuffer_get_write_vector (driver->outbound_events, vec);

  dev = (struct a2j_delivery_event*) vec[0].buf;
  limit = vec[0].len / sizeof (struct a2j_delivery_event);
  nevents = jack_midi_get_event_count (port->jack_buf);

  for (i = 0; (i < nevents) && (written < limit); ++i) {

    jack_midi_event_get (&dev->jack_event, port->jack_buf, i);
    if (dev->jack_event.size <= MAX_JACKMIDI_EV_SIZE)
    {
      dev->time = dev->jack_event.time;
      dev->port = port;
      memcpy( dev->midistring, dev->jack_event.buffer, dev->jack_event.size );
      written++;
      ++dev;
    }
  }

  /* anything left? use the second part of the vector, as much as possible */

  if (i < nevents)
  {
    if (vec[0].len)
    {
      gap = vec[0].len - written * sizeof(struct a2j_delivery_event);
    }

    dev = (struct a2j_delivery_event*) vec[1].buf;

    limit += (vec[1].len / sizeof (struct a2j_delivery_event));

    while ((i < nevents) && (written < limit))
    {
      jack_midi_event_get(&dev->jack_event, port->jack_buf, i);
      if (dev->jack_event.size <= MAX_JACKMIDI_EV_SIZE)
      {
        dev->time = dev->jack_event.time;
        dev->port = port;
        memcpy(dev->midistring, dev->jack_event.buffer, dev->jack_event.size);
        written++;
        ++dev;
      } 
      ++i;
    }
  }

  a2j_debug( "done pushing events: %d ... gap: %d ", (int)written, (int)gap );
  /* clear JACK port buffer; advance ring buffer ptr */

  jack_ringbuffer_write_advance (driver->outbound_events, written * sizeof (struct a2j_delivery_event) + gap);

  return nevents;
}

static int
time_sorter (struct a2j_delivery_event * a, struct a2j_delivery_event * b)
{
  if (a->time < b->time) {
    return -1;
  } else if (a->time > b->time) {
    return 1;
  } 
  return 0;
}

static void* 
alsa_output_thread(void * arg)
{
  alsa_midi_driver_t * driver = (alsa_midi_driver_t*) arg;
  struct a2j_stream *str = &driver->stream[A2J_PORT_PLAYBACK];
  int i;
  struct list_head evlist;
  struct list_head * node_ptr;
  jack_ringbuffer_data_t vec[2];
  snd_seq_event_t alsa_event;
  struct a2j_delivery_event* ev;
  float sr;
  jack_nframes_t now;
  int err;
  int limit;

  while (driver->running) {
    /* pre-first, handle port deletion requests */

    a2j_free_ports(driver);

    /* first, make a list of all events in the outbound_events FIFO */
    
    INIT_LIST_HEAD(&evlist);

    jack_ringbuffer_get_read_vector (driver->outbound_events, vec);

    a2j_debug ("output thread: got %d+%d events", 
               (vec[0].len / sizeof (struct a2j_delivery_event)),
               (vec[1].len / sizeof (struct a2j_delivery_event)));
    
    ev = (struct a2j_delivery_event*) vec[0].buf;
    limit = vec[0].len / sizeof (struct a2j_delivery_event);
    for (i = 0; i < limit; ++i) {
      list_add_tail(&ev->siblings, &evlist);
      ev++;
    }

    ev = (struct a2j_delivery_event*) vec[1].buf;
    limit = vec[1].len / sizeof (struct a2j_delivery_event);
    for (i = 0; i < limit; ++i) {
      list_add_tail(&ev->siblings, &evlist);
      ev++;
    }

    if (vec[0].len < sizeof(struct a2j_delivery_event) && (vec[1].len == 0)) {
      /* no events: wait for some */
      a2j_debug ("output thread: wait for events");
      sem_wait (&driver->output_semaphore);
      a2j_debug ("output thread: AWAKE ... loop back for events");
      continue;
    }

    /* now sort this list by time */

    list_sort(&evlist, struct a2j_delivery_event, siblings, time_sorter);

    /* now deliver */

    sr = jack_get_sample_rate (driver->jack_client);

    list_for_each(node_ptr, &evlist)
    {
      ev = list_entry(node_ptr, struct a2j_delivery_event, siblings);

      snd_seq_ev_clear(&alsa_event);
      snd_midi_event_reset_encode(str->codec);
      if (!snd_midi_event_encode(str->codec, (const unsigned char *)ev->midistring, ev->jack_event.size, &alsa_event))
      {
        continue; // invalid event
      }
      
      snd_seq_ev_set_source(&alsa_event, driver->port_id);
      snd_seq_ev_set_dest(&alsa_event, ev->port->remote.client, ev->port->remote.port);
      snd_seq_ev_set_direct (&alsa_event);
      
      now = jack_frame_time (driver->jack_client);

      ev->time += driver->cycle_start;

      a2j_debug ("@ %d, next event @ %d", now, ev->time);
      
      /* do we need to wait a while before delivering? */

      if (ev->time > now) {
        struct timespec nanoseconds;
        jack_nframes_t sleep_frames = ev->time - now;
        float seconds = sleep_frames / sr;

        /* if the gap is long enough, sleep */

        if (seconds > 0.001) {
          nanoseconds.tv_sec = (time_t) seconds;
          nanoseconds.tv_nsec = (long) NSEC_PER_SEC * (seconds - nanoseconds.tv_sec);
          
          a2j_debug ("output thread sleeps for %.2f msec", ((double) nanoseconds.tv_nsec / NSEC_PER_SEC) * 1000.0);

          if (nanosleep (&nanoseconds, NULL) < 0) {
            fprintf (stderr, "BAD SLEEP\n");
            /* do something ? */
          }
        }
      }
      
      /* its time to deliver */
      err = snd_seq_event_output(driver->seq, &alsa_event);
      snd_seq_drain_output (driver->seq);
      now = jack_frame_time (driver->jack_client);
      a2j_debug("alsa_out: written %d bytes to %s at %d, DELTA = %d", ev->jack_event.size, ev->port->name, now, 
                (int32_t) (now - ev->time));
    }

    /* free up space in the FIFO */
    
    jack_ringbuffer_read_advance (driver->outbound_events, vec[0].len + vec[1].len);

    /* and head back for more */
  }

  return (void*) 0;
}

/** CORE JACK PROCESSING */


/* ALSA */

static void
a2j_jack_process_internal (alsa_midi_driver_t* driver, int dir, jack_nframes_t nframes)
{
  struct a2j_stream * stream_ptr;
  int i;
  struct a2j_port ** port_ptr_ptr;
  struct a2j_port * port_ptr;
  int nevents = 0;

  stream_ptr = &driver->stream[dir];
  a2j_add_ports(stream_ptr);

  // process ports
  for (i = 0 ; i < PORT_HASH_SIZE ; i++)
  {
    port_ptr_ptr = &stream_ptr->port_hash[i];
    while (*port_ptr_ptr != NULL)
    {
      port_ptr = *port_ptr_ptr;

      if (!port_ptr->is_dead) {
        port_ptr->jack_buf = jack_port_get_buffer(port_ptr->jack_port, nframes);
        
        if (dir == A2J_PORT_CAPTURE) {
          a2j_process_incoming (driver, port_ptr, nframes);
        } else {
          nevents += a2j_process_outgoing (driver, port_ptr);
        }
        
      } else if (jack_ringbuffer_write_space (driver->port_del) >= sizeof(port_ptr)) {
        
        a2j_debug("jack: removed port %s", port_ptr->name);
        *port_ptr_ptr = port_ptr->next;
        jack_ringbuffer_write(driver->port_del, (char*)&port_ptr, sizeof(port_ptr));
        nevents += 1; /* wake up output thread, see: a2j_free_ports */
        continue;
        
      }

      port_ptr_ptr = &port_ptr->next;
    }
  }

  if (dir == A2J_PORT_PLAYBACK &&  nevents > 0) {
    int sv;

    /* if we queued up anything for output, tell the output thread in 
       case its waiting for us.
    */
    
    sem_getvalue (&driver->output_semaphore, &sv);
    sem_post (&driver->output_semaphore);
  } 
}

/* JACK DRIVER FUNCTIONS */

static int
alsa_midi_read (alsa_midi_driver_t* driver, jack_nframes_t nframes)
{
  driver->cycle_start = jack_last_frame_time (driver->jack_client);
  a2j_jack_process_internal (driver, A2J_PORT_CAPTURE, nframes);
  return 0;
}

static int
alsa_midi_write (alsa_midi_driver_t* driver, jack_nframes_t nframes)
{
  driver->cycle_start = jack_last_frame_time (driver->jack_client);
  a2j_jack_process_internal (driver, A2J_PORT_PLAYBACK, nframes);
  return 0;
}


static int 
alsa_midi_start (alsa_midi_driver_t* driver)
{
  int error;
        
  snd_seq_start_queue (driver->seq, driver->queue, 0); 
  snd_seq_drop_input (driver->seq);
  
  a2j_add_ports(&driver->stream[A2J_PORT_CAPTURE]);
  a2j_add_ports(&driver->stream[A2J_PORT_PLAYBACK]);
  
  driver->running = true;
  
  if (pthread_create(&driver->alsa_input_thread, NULL, alsa_input_thread, driver) < 0) {
    a2j_error("cannot start ALSA input thread");
    return -1;
  }
  
  /* wake the poll loop in the alsa input thread so initial ports are fetched */
  if ((error = snd_seq_connect_from (driver->seq, driver->port_id, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE)) < 0) {
    a2j_error("snd_seq_connect_from() failed");
    return -1;
  }
  
  if (pthread_create(&driver->alsa_output_thread, NULL, alsa_output_thread, driver) < 0) {
    a2j_error("cannot start ALSA input thread");
    return -1;
  }
  
  return 0;
}

static int
alsa_midi_stop (alsa_midi_driver_t* driver)
{
  (void) snd_seq_stop_queue (driver->seq, driver->queue, 0); 
  return 0;
}

static int
alsa_midi_attach (alsa_midi_driver_t* driver, jack_engine_t* engine)
{
  int error;
  
  driver->port_del = jack_ringbuffer_create(2 * MAX_PORTS * sizeof(struct a2j_port *));
  if (driver->port_del == NULL) {
    return -1;
  }
  
  driver->outbound_events = jack_ringbuffer_create (MAX_EVENT_SIZE * 16 * sizeof(struct a2j_delivery_event));
  if (driver->outbound_events == NULL) {
    return -1;
  }
        
  if (!a2j_stream_init (driver, A2J_PORT_CAPTURE)) {
    return -1;
  }

  if (!a2j_stream_init (driver, A2J_PORT_PLAYBACK)) {
    return -1;
  }

  if ((error = snd_seq_open(&driver->seq, "hw", SND_SEQ_OPEN_DUPLEX, 0)) < 0) {
    a2j_error("failed to open alsa seq");
    return -1;
  }

  if ((error = snd_seq_set_client_name(driver->seq, "jackmidi")) < 0) {
    a2j_error("snd_seq_set_client_name() failed");
    return -1;
  }

  if ((driver->port_id = snd_seq_create_simple_port(
         driver->seq,
         "port",
         SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_WRITE
#ifndef DEBUG
         |SND_SEQ_PORT_CAP_NO_EXPORT
#endif
         ,SND_SEQ_PORT_TYPE_APPLICATION)) < 0) {

    a2j_error("snd_seq_create_simple_port() failed");
    return -1;
  }

  if ((driver->client_id = snd_seq_client_id(driver->seq)) < 0) {
    a2j_error("snd_seq_client_id() failed");
    return -1;
  }
	
  if ((driver->queue = snd_seq_alloc_queue(driver->seq)) < 0) {
    a2j_error("snd_seq_alloc_queue() failed");
    return -1;
  }

  if ((error = snd_seq_nonblock(driver->seq, 1)) < 0) {
    a2j_error("snd_seq_nonblock() failed");
    return -1;
  }

  return jack_activate (driver->jack_client);
}

static int
alsa_midi_detach (alsa_midi_driver_t* driver, jack_engine_t* engine)
{
  driver->finishing = true;
  
  stop_threads (driver);
  snd_seq_close (driver->seq);
  driver->seq = NULL;
  return 0;
}

static jack_driver_t *
alsa_midi_driver_new (jack_client_t *client, const char *name)
{
	alsa_midi_driver_t* driver = calloc(1, sizeof(alsa_midi_driver_t));

	jack_info ("creating alsa_midi driver ..."); 

	if (!driver) {
		return NULL;
	}

	jack_driver_init ((jack_driver_t *) driver);

	driver->attach = (JackDriverAttachFunction) alsa_midi_attach;
	driver->detach = (JackDriverDetachFunction) alsa_midi_detach;
	driver->read = (JackDriverReadFunction) alsa_midi_read;
	driver->write = (JackDriverWriteFunction) alsa_midi_write;
	driver->start = (JackDriverStartFunction) alsa_midi_start;
	driver->stop = (JackDriverStartFunction) alsa_midi_stop;

	driver->jack_client = client;

        if (sem_init(&driver->output_semaphore, 0, 0) < 0) {
          a2j_error ("can't create IO semaphore");
          free (driver);
          return NULL;
        }

	return (jack_driver_t *) driver;
}

static void
alsa_midi_driver_delete (alsa_midi_driver_t* driver)
{
  a2j_stream_detach (&driver->stream[A2J_PORT_CAPTURE]);
  a2j_stream_detach (&driver->stream[A2J_PORT_PLAYBACK]);
  a2j_stream_close (driver, A2J_PORT_CAPTURE);
  a2j_stream_close (driver, A2J_PORT_PLAYBACK);
  
  sem_destroy (&driver->output_semaphore);

  jack_ringbuffer_free (driver->outbound_events);
  jack_ringbuffer_free (driver->port_del);
}

/* DRIVER "PLUGIN" INTERFACE */

const char driver_client_name[] = "alsa_midi";

const jack_driver_desc_t *
driver_get_descriptor ()
{
	jack_driver_desc_t * desc;
	jack_driver_param_desc_t * params;
	//unsigned int i;

	desc = calloc (1, sizeof (jack_driver_desc_t));

	strcpy (desc->name,"alsa_midi");
	desc->nparams = 0;
  
	params = calloc (desc->nparams, sizeof (jack_driver_param_desc_t));

	desc->params = params;

	return desc;
}

jack_driver_t *
driver_initialize (jack_client_t *client, const JSList * params)
{
	const JSList * node;
	const jack_driver_param_t * param;

	for (node = params; node; node = jack_slist_next (node)) {
  	        param = (const jack_driver_param_t *) node->data;

		switch (param->character) {
			default:
				break;
		}
	}
			
	return alsa_midi_driver_new (client, NULL);
}

void
driver_finish (jack_driver_t *driver)
{
	alsa_midi_driver_delete ((alsa_midi_driver_t *) driver);
}
