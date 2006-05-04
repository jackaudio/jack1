/*
    Copyright (C) 2004 Ian Esten
    
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

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/port.h>

/* even though the new implementation with byte offsets being stored instead of
 * pointers, last_write_loc can't be removed as it gets used in the mixdown
 * function. */   
typedef struct _jack_midi_port_info_private
{
	jack_midi_port_info_t	info;
	jack_nframes_t			last_write_loc;
	char					last_status;		/* status byte for last event in buffer */
	jack_nframes_t			events_lost;		/* number of events lost in this buffer.
												 * mixdown is the only place that sets
												 * this for now */
} jack_midi_port_info_private_t;

typedef struct _jack_midi_port_internal_event
{
	jack_nframes_t time;
	size_t size;
	size_t byte_offset;
} jack_midi_port_internal_event_t;


jack_midi_port_info_t*
jack_midi_port_get_info(void* port_buffer, jack_nframes_t nframes)
{
	return (jack_midi_port_info_t*)port_buffer;
}


int
jack_midi_event_get(jack_midi_event_t* event, void* port_buffer, jack_nframes_t event_idx, jack_nframes_t nframes)
{
	jack_midi_port_internal_event_t* port_event;
	jack_midi_port_info_private_t* info = (jack_midi_port_info_private_t*)port_buffer;
	if(event_idx >= info->info.event_count)
		return ENODATA;
	port_event = (jack_midi_port_internal_event_t*)(info+1);
	port_event += event_idx;
	event->time = port_event->time;
	event->size = port_event->size;
	event->buffer = ((jack_midi_data_t*)port_buffer) + port_event->byte_offset;
	return 0;
}


jack_midi_data_t*
jack_midi_event_reserve(void* port_buffer, jack_nframes_t time, size_t data_size, jack_nframes_t nframes)
{
	jack_midi_data_t* retbuf = (jack_midi_data_t*)port_buffer;
	/* bad: below line needs to know about buffer scale factor */
	jack_nframes_t buffer_size = nframes*sizeof(jack_default_audio_sample_t)/sizeof(unsigned char);
	jack_midi_port_info_private_t* info = (jack_midi_port_info_private_t*)port_buffer;
	jack_midi_port_internal_event_t* event_buffer = (jack_midi_port_internal_event_t*)(info + 1);
	/* check there is enough space in the buffer for the event. */
	if(info->last_write_loc + sizeof(jack_midi_port_info_private_t)
		+ (info->info.event_count + 1) * sizeof(jack_midi_port_internal_event_t)
		+ data_size > buffer_size)
		return NULL;
	else
	{
		info->last_write_loc += data_size;
		retbuf = &retbuf[buffer_size - 1 - info->last_write_loc];
		event_buffer[info->info.event_count].time = time;
		event_buffer[info->info.event_count].size = data_size;
		event_buffer[info->info.event_count].byte_offset = buffer_size - 1 - info->last_write_loc;
		info->info.event_count += 1;
		return retbuf;
	}
}


int
jack_midi_event_write(void* port_buffer, jack_nframes_t time, jack_midi_data_t* data, size_t data_size, jack_nframes_t nframes)
{
	jack_midi_data_t* retbuf = jack_midi_event_reserve(port_buffer, time, data_size, nframes);
	if(retbuf)
	{
		memcpy(retbuf, data, data_size);
		return 0;
	}
	else
		return ENOBUFS;
}


/*jack_midi_data_t jack_midi_get_last_status_byte(void* port_buffer, jack_nframes_t nframes)
{
	return ((jack_midi_port_info_private_t*)(port_buffer))->last_status;
}*/


jack_midi_data_t jack_midi_get_status_before_event_n(void* port_buffer, jack_nframes_t event_index, jack_nframes_t nframes)
{
	jack_midi_data_t* event_buf = (jack_midi_data_t*)port_buffer;
	jack_midi_port_info_private_t* info = (jack_midi_port_info_private_t*)port_buffer;
	jack_midi_port_internal_event_t* event_buffer = (jack_midi_port_internal_event_t*)(info + 1);
	
	/* safeguard against unitialised ports */
	if(info->info.event_count * sizeof(jack_midi_port_internal_event_t) + sizeof(jack_midi_port_info_private_t)
	   > nframes * sizeof(jack_default_audio_sample_t))
		return 0;
	/* don't know what to do here, so we return 0 */
	if(event_index >= info->info.event_count)
		return 0;
	if(info->info.event_count > 0)
	{
		event_index -= 1;
		while( (event_index >= 0)
				&& (event_buf[event_buffer[event_index].byte_offset] < 0x80)
				&& (event_buf[event_buffer[event_index].byte_offset] > 0xf7) )
			event_index--;
		if(event_index >= 0)
			return event_buf[event_buffer[event_index].byte_offset];
	}
	/* all the events in the port are running status, so we return the previous buffers last
	 * status byte */
	return info->last_status;
}


/* this function can be got rid of now, as the above function replaces it and is more general */
static jack_midi_data_t get_last_status_byte(void* port_buffer, jack_nframes_t nframes)
{
	int event_index = 0;
	jack_midi_data_t* event_buf = (jack_midi_data_t*)port_buffer;
	jack_midi_port_info_private_t* info = (jack_midi_port_info_private_t*)port_buffer;
	jack_midi_port_internal_event_t* event_buffer = (jack_midi_port_internal_event_t*)(info + 1);
	
	/* safeguard against unitialised ports */
	if(info->info.event_count * sizeof(jack_midi_port_internal_event_t) + sizeof(jack_midi_port_info_private_t)
	   > nframes * sizeof(jack_default_audio_sample_t))
		return 0;
	if(info->info.event_count > 0)
	{
		event_index = info->info.event_count - 1;
		while( (event_index >= 0)
				&& (event_buf[event_buffer[event_index].byte_offset] < 0x80)
				&& (event_buf[event_buffer[event_index].byte_offset] > 0xf7) )
			event_index--;
		if(event_index >= 0)
			return event_buf[event_buffer[event_index].byte_offset];
	}
	return info->last_status;
}


/* can't check to make sure this port is an output anymore. if this gets
 * called on an input port, all clients after the client that calls it
 * will think there are no events in the buffer as the event count has
 * been reset
 * TODO: this function must take note of system realtime messages and
 * skip them when updating last_status. done. */
void jack_midi_clear_buffer(void* port_buffer, jack_nframes_t nframes)
{
	jack_midi_port_info_private_t* info = (jack_midi_port_info_private_t*)port_buffer;
	if(port_buffer == NULL)
	{
/*		fprintf(stderr, "Error: port_buffer in jack_midi_clear_buffer is NULL!\n");*/
		return;
	}
	/* find the last status byte in the buffer for use in the mixdown function */
	/* this code is dangerous when used on a freshly allocated buffer that has
	 * not has jack_midi_reset_new_port called on it, because the event_count
	 * may be some huge number... */
	info->last_status = get_last_status_byte(port_buffer, nframes);
	info->info.event_count = 0;
	info->last_write_loc = 0;
	info->events_lost = 0;
}


void jack_midi_reset_new_port(void* port_buffer, jack_nframes_t nframes)
{
	jack_midi_port_info_private_t* info = (jack_midi_port_info_private_t*)port_buffer;
	info->info.event_count = 0;
	info->last_write_loc = 0;
	info->last_status = 0;
	info->events_lost = 0;
}


void jack_midi_port_mixdown (jack_port_t *port, jack_nframes_t nframes)
{
	JSList *node;
	jack_port_t *input;
	jack_nframes_t num_events = 0, i = 0;
	int err = 0;
	/* smallest_info is for the data buffer holding the next event */
	jack_midi_port_info_private_t *in_info, *out_info, *smallest_info;
	/* smallest_event is for the event buffer holding the next event */
	jack_midi_port_internal_event_t *in_events, *smallest_event, *out_events;
	/* for assembling new events if running status is interrupted */
	jack_midi_data_t new_event_buf[3];
	jack_midi_data_t *smallest_buffer = NULL;
	
	out_info = (jack_midi_port_info_private_t*)port->mix_buffer;
	out_events = (jack_midi_port_internal_event_t*)(out_info + 1);
	/* cache last status byte */
	out_info->last_status = get_last_status_byte(port->mix_buffer, nframes);

	out_info->info.event_count = 0;
	
	/* initialise smallest_info to point to the first port buffer */
	smallest_info = (jack_midi_port_info_private_t*)jack_output_port_buffer((jack_port_t*)(port->connections->data));
	smallest_event = (jack_midi_port_internal_event_t*)(smallest_info + 1);

	/* in this loop we can use last_write_loc in jack_midi_port_info_private_t
	 * to store indexes of the last event read from that buffer. this is ok
	 * because last_write_loc is used when writing events to a buffer, which
	 * is already complete.
	 */
	for(node = port->connections; node; node = jack_slist_next (node))
	{
		input = (jack_port_t *) node->data;
		in_info = (jack_midi_port_info_private_t*)jack_output_port_buffer (input);
		num_events += in_info->info.event_count;
		in_info->last_write_loc = 0;
		/* look to see if first event in each buffer is running status */
	}
		
	printf(" jack_midi_port_mixdown got %d events\n", num_events);

	while(i<num_events)
	{
		node = port->connections;
		/* had something else to do here, to do with smallest_info/buf? */
		while(node)
		{
			in_info = (jack_midi_port_info_private_t*)jack_output_port_buffer (((jack_port_t *) node->data));
			in_events = (jack_midi_port_internal_event_t*)(in_info + 1);
			/* make sure there are events left in this port */
			if(in_info->info.event_count > in_info->last_write_loc)
			{
				/* (first_event_in_buffer_timestamp < smallest_timestamp_event) */
				if(in_events[in_info->last_write_loc].time <= smallest_event[smallest_info->last_write_loc].time)
				{
					smallest_info = in_info;
					smallest_event = (jack_midi_port_internal_event_t*)(&in_events[in_info->last_write_loc]);
					smallest_buffer = (jack_midi_data_t*)in_info;
				}
			}
			/* else buffer has no events remaining in it */
			node = jack_slist_next(node);
		}
		/* write event to output port.
		 * things to consider:
		 * if event is running status, make sure previous event in its port is of the same
		 * type as the last event in the output port.
		 */
		
		
		
		/* test to see if this event is running status, or 1 byte realtime msg */
		if( (smallest_buffer[smallest_event[smallest_info->last_write_loc].byte_offset] < 0x80)
			&& ((*new_event_buf = get_last_status_byte((void*)smallest_info, nframes))
				!= get_last_status_byte((void*)port->mix_buffer, nframes)) )
		{
			memcpy(new_event_buf + 1,
				   &smallest_buffer[smallest_event[smallest_info->last_write_loc].byte_offset],
				   smallest_event[smallest_info->last_write_loc].size);
			err = jack_midi_event_write(jack_output_port_buffer((jack_port_t*)node->data),
										smallest_event[smallest_info->last_write_loc].time,
										new_event_buf,
										smallest_event[smallest_info->last_write_loc].size + 1,
										nframes);
		}
		else
		{
			err = jack_midi_event_write(jack_output_port_buffer((jack_port_t*)node->data),
										smallest_event[smallest_info->last_write_loc].time,
										&smallest_buffer[smallest_event[smallest_info->last_write_loc].byte_offset],
										smallest_event[smallest_info->last_write_loc].size,
										nframes);
		}
		if(err < 0)
		{
			out_info->events_lost = num_events - i - 1;
			break;
		}
		else
			i++;
	}
}


jack_nframes_t jack_midi_get_lost_event_count(void* port_buffer, jack_nframes_t nframes)
{
	return ((jack_midi_port_info_private_t*)port_buffer)->events_lost;
}
