/*
    Copyright (C) 2004-2006 Ian Esten
    Copyright (C) 2006 Dave Robillard
	
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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/port.h>


typedef struct _jack_midi_port_info_private {
	jack_nframes_t        nframes; /**< Number of frames in buffer */
 	size_t                buffer_size; /**< Size of buffer in bytes */
	jack_nframes_t        event_count; /**< Number of events stored in this buffer */
	jack_nframes_t        last_write_loc; /**< Used for both writing and mixdown */
	jack_nframes_t        events_lost;	  /**< Number of events lost in this buffer */
} jack_midi_port_info_private_t;

typedef struct _jack_midi_port_internal_event {
	int32_t        time;
	jack_shmsize_t size;
	jack_shmsize_t byte_offset;
} jack_midi_port_internal_event_t;


/* jack_midi_port_functions.buffer_init */
static void
jack_midi_buffer_init(void  *port_buffer,
                       size_t buffer_size,
		       jack_nframes_t nframes)
{
	jack_midi_port_info_private_t *info =
		(jack_midi_port_info_private_t *) port_buffer;
	/* We can also add some magic field to midi buffer to validate client calls */
	info->nframes = nframes;
	info->buffer_size = buffer_size;
	info->event_count = 0;
	info->last_write_loc = 0;
	info->events_lost = 0;
}


jack_nframes_t
jack_midi_get_event_count(void           *port_buffer)
{
	jack_midi_port_info_private_t *info =
		(jack_midi_port_info_private_t *) port_buffer;
	return info->event_count;
}


int
jack_midi_event_get(jack_midi_event_t *event,
                    void              *port_buffer,
                    jack_nframes_t     event_idx)
{
	jack_midi_port_internal_event_t *port_event;
	jack_midi_port_info_private_t *info =
		(jack_midi_port_info_private_t *) port_buffer;
	
	if (event_idx >= info->event_count)
		return ENODATA;
	
	port_event = (jack_midi_port_internal_event_t *) (info + 1);
	port_event += event_idx;
	event->time = port_event->time;
	event->size = port_event->size;
	event->buffer =
		((jack_midi_data_t *) port_buffer) + port_event->byte_offset;
	
	return 0;
}


size_t
jack_midi_max_event_size(void           *port_buffer)
{
	jack_midi_port_info_private_t *info =
		(jack_midi_port_info_private_t *) port_buffer;
	size_t buffer_size =
		info->buffer_size;
	
	/* (event_count + 1) below accounts for jack_midi_port_internal_event_t
	 * which would be needed to store the next event */
	size_t used_size = sizeof(jack_midi_port_info_private_t)
		+ info->last_write_loc
		+ ((info->event_count + 1)
		   * sizeof(jack_midi_port_internal_event_t));
	
	if (used_size > buffer_size)
		return 0;
	else
		return (buffer_size - used_size);
}


jack_midi_data_t*
jack_midi_event_reserve(void           *port_buffer,
                        jack_nframes_t  time, 
                        size_t          data_size)
{
	jack_midi_data_t *retbuf = (jack_midi_data_t *) port_buffer;

	jack_midi_port_info_private_t *info =
		(jack_midi_port_info_private_t *) port_buffer;
	jack_midi_port_internal_event_t *event_buffer =
		(jack_midi_port_internal_event_t *) (info + 1);
	size_t buffer_size =
		info->buffer_size;
	
	if (time < 0 || time >= info->nframes)
 		goto failed;
 
 	if (info->event_count > 0 && time < event_buffer[info->event_count-1].time)
 		goto failed;

	/* Check if data_size is >0 and there is enough space in the buffer for the event. */
	if (data_size <=0 ||
	    info->last_write_loc + sizeof(jack_midi_port_info_private_t)
			+ ((info->event_count + 1)
			   * sizeof(jack_midi_port_internal_event_t))
			+ data_size > buffer_size) {
		goto failed;
	} else {
		info->last_write_loc += data_size;
		retbuf = &retbuf[buffer_size - 1 - info->last_write_loc];
		event_buffer[info->event_count].time = time;
		event_buffer[info->event_count].size = data_size;
		event_buffer[info->event_count].byte_offset =
			buffer_size - 1 - info->last_write_loc;
		info->event_count += 1;
		return retbuf;
	}
 failed:
 	info->events_lost++;
	return NULL;
}


int
jack_midi_event_write(void                   *port_buffer,
                      jack_nframes_t          time,
                      const jack_midi_data_t *data,
                      size_t                  data_size)
{
	jack_midi_data_t *retbuf =
		jack_midi_event_reserve(port_buffer, time, data_size);

	if (retbuf) {
		memcpy(retbuf, data, data_size);
		return 0;
	} else {
		return ENOBUFS;
	}
}


/* Can't check to make sure this port is an output anymore.  If this gets
 * called on an input port, all clients after the client that calls it
 * will think there are no events in the buffer as the event count has
 * been reset.
 */
void
jack_midi_clear_buffer(void           *port_buffer)
{
	jack_midi_port_info_private_t *info =
		(jack_midi_port_info_private_t *) port_buffer;

	info->event_count = 0;
	info->last_write_loc = 0;
	info->events_lost = 0;
}


/* jack_midi_port_functions.mixdown */
static void
jack_midi_port_mixdown(jack_port_t    *port, jack_nframes_t nframes)
{
	JSList         *node;
	jack_port_t    *input;
	jack_nframes_t  num_events = 0;
	jack_nframes_t  i          = 0;
	int             err        = 0;
	jack_nframes_t  lost_events = 0;

	/* The next (single) event to mix in to the buffer */
	jack_midi_port_info_private_t   *earliest_info;
	jack_midi_port_internal_event_t *earliest_event;
	jack_midi_data_t                *earliest_buffer;
	
	jack_midi_port_info_private_t   *in_info;   /* For finding next event */
	jack_midi_port_internal_event_t *in_events; /* Corresponds to in_info */
	jack_midi_port_info_private_t   *out_info;  /* Output 'buffer' */

	jack_midi_clear_buffer(port->mix_buffer);
	
	out_info = (jack_midi_port_info_private_t *) port->mix_buffer;

	/* This function uses jack_midi_port_info_private_t.last_write_loc of the
	 * source ports to store indices of the last event read from that buffer
	 * so far.  This is OK because last_write_loc is used when writing events
	 * to a buffer, which at this stage is already complete so the value
	 * can be safely smashed. */
	
	/* Iterate through all connections to see how many events we need to mix,
	 * and initialise their 'last event read' (last_write_loc) to 0 */
	for (node = port->connections; node; node = jack_slist_next(node)) {
		input = (jack_port_t *) node->data;
		in_info =
			(jack_midi_port_info_private_t *) jack_output_port_buffer(input);
		num_events += in_info->event_count;
		lost_events += in_info->events_lost;
		in_info->last_write_loc = 0;
	}

	/* Write the events in the order of their timestamps */
	for (i = 0; i < num_events; ++i) {
		earliest_info = NULL;
		earliest_event = NULL;
		earliest_buffer = NULL;

		/* Find the earliest unread event, to mix next
		 * (search for an event earlier than earliest_event) */
		for (node = port->connections; node; node = jack_slist_next(node)) {
			in_info = (jack_midi_port_info_private_t *)
				jack_output_port_buffer(((jack_port_t *) node->data));
			in_events = (jack_midi_port_internal_event_t *) (in_info + 1);

			/* If there are unread events left in this port.. */
			if (in_info->event_count > in_info->last_write_loc) {
				/* .. and this event is the new earliest .. */
				/* NOTE: that's why we compare time with <, not <= */
				if (earliest_info == NULL
						|| in_events[in_info->last_write_loc].time
					       < earliest_event->time) {
					/* .. then set this event as the next earliest */
					earliest_info = in_info;
					earliest_event = (jack_midi_port_internal_event_t *)
						(&in_events[in_info->last_write_loc]);
				}
			}
		}

		if (earliest_info && earliest_event) {
			earliest_buffer = (jack_midi_data_t *) earliest_info;
			
			/* Write event to output */
			err = jack_midi_event_write(
				jack_port_buffer(port),
				earliest_event->time,
				&earliest_buffer[earliest_event->byte_offset],
				earliest_event->size);
			
			earliest_info->last_write_loc++;

			if (err) {
				out_info->events_lost = num_events - i;
				break;
			}
		}
	}
	assert(out_info->event_count == num_events - out_info->events_lost);

	// inherit total lost events count from all connected ports.
	out_info->events_lost += lost_events;
}


jack_nframes_t
jack_midi_get_lost_event_count(void           *port_buffer)
{
	return ((jack_midi_port_info_private_t *) port_buffer)->events_lost;
}

jack_port_functions_t jack_builtin_midi_functions = {
	.buffer_init    = jack_midi_buffer_init,
	.mixdown = jack_midi_port_mixdown, 
};
