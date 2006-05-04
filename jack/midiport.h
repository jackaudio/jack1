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


#ifndef __JACK_MIDIPORT_H
#define __JACK_MIDIPORT_H

#ifdef __cplusplus
extern "C" {
#endif
	
#include <jack/types.h>
#include <stdlib.h>

/** Type for raw event data contained in @ref jack_midi_event_t. */
typedef unsigned char jack_midi_data_t;

/* buffer is a pointer to the midi data. time is the sample index at which this
 * event is valid. size_t is how many bytes of data are in 'buffer'. the events
 * in 'buffer' are standard midi messages. note that there is no event type field
 * anymore. that is now just byte 0 of the buffer.
 */
typedef struct _jack_midi_event
{
	jack_nframes_t time;
	size_t size;
	jack_midi_data_t* buffer;
} jack_midi_event_t;

typedef struct _jack_midi_port_info
{
	jack_nframes_t event_count;
} jack_midi_port_info_t;

/* returns an info struct. only info in there at the moment is how many
 * events are in the buffer
 */
jack_midi_port_info_t* jack_midi_port_get_info(void* port_buffer, jack_nframes_t nframes);


/** Get a MIDI event from an event port buffer.
 * 
 * The MIDI event returned is guaranteed to be a complete MIDI
 * event (i.e. clients do not have to deal with running status
 * as the status byte of the event will always be present).
 *
 * @param event Event structure to store retrieved event in.
 * @param port_buffer Port buffer from which to retrieve event.
 * @param event_index Index of event to retrieve.
 * @param nframes Number of valid frames this cycle.
 * @return 0 on success, ENODATA if buffer is empty.
 */
int jack_midi_event_get(jack_midi_event_t* event, void* port_buffer, jack_nframes_t event_idx, jack_nframes_t nframes);


/** Initialise the port state. This must be used after port_buffer is allocated */
void jack_midi_reset_new_port(void* port_buffer, jack_nframes_t nframes);


/** Clear an event buffer.
 * 
 * This should be called at the beginning of each process cycle before calling
 * @ref jack_midi_event_reserve or @ref jack_midi_event_write. This
 * function may not be called on an input port's buffer.
 *
 * @param port_buffer Port buffer to clear (must be an output port buffer).
 * @param nframes Number of valid frames this cycle.
 */
void jack_midi_clear_buffer(void* port_buffer, jack_nframes_t nframes);


/** Allocate space for an event to be written to an event port buffer.
 *
 * Clients are to write the actual event data to be written starting at the
 * pointer returned by this function. Clients must not write more than
 * @a data_size bytes into this buffer.
 *
 * @param port_buffer Buffer to write event to.
 * @param time Sample offset of event.
 * @param data_size Length of event's raw data in bytes.
 * @param nframes Number of valid frames this event.
 * @return Pointer to the beginning of the reserved event's data buffer, or
 * NULL on error (ie not enough space).
 */
jack_midi_data_t* jack_midi_event_reserve(void* port_buffer, jack_nframes_t time, 
										  size_t data_size, jack_nframes_t nframes);


/** Write an event into an event port buffer.
 *
 * This function is simply a wrapper for @ref jack_midi_event_reserve
 * which writes the event data into the space reserved in the buffer.
 * 
 * @param port_buffer Buffer to write event to.
 * @param time Sample offset of event.
 * @param data Message data to be written.
 * @param data_size Length of @ref data in bytes.
 * @param nframes Number of valid frames this event.
 * @return 0 on success, ENOBUFS if there's not enough space in buffer for event.
 */
int jack_midi_event_write(void* port_buffer, jack_nframes_t time, jack_midi_data_t* data, size_t data_size, jack_nframes_t nframes);



/* function to make dealing with running status easier. calls to write update the
 * running status byte stored in a port, and this function returns it. note that
 * when a port is initialised, last_status_byte is set to 0, which is not a valid
 * status byte. valid status bytes are in the range 0x80 <= valid < 0xf8 */
/* this function cannot be supported because it cannot be guaranteed that the
 * last status byte stored will be correct. it was intended that
 * jack_midi_write_next_event would cache the status byte from the previous write.
 * this assumes that users are going to alternate calls to
 * jack_midi_write_next_event with writing of actual data. it is a pity that this
 * can't be forced as it makes things more efficient. it would have be made clear
 * in the api doc that users must write data after calling
 * jack_midi_write_next_event for this function to be implementable. either that
 * or jack_midi_write_next_event must be removed from the api, and only
 * jack_midi_write_next_event2 would be supported. jack_midi_write_next_event is
 * heavily used in the hardware i/o client, and removing it would make the input
 * thread more complex and use more memory.
 * the other way of making this function possible to implement is to force users
 * to pass in the first byte of the message (which they should know, as the
 * message size they request depends on it). this is probably the best way to do
 * things as now that i think about it, the first method suggested in this
 * comment might result in erroneous status byte reporting as last_status_byte
 * updated with a delay of one message */
/*jack_midi_data_t jack_midi_get_last_status_byte(void* port_buffer, jack_nframes_t nframes);*/


/** return the last status byte in the stream before event n */
jack_midi_data_t jack_midi_get_status_before_event_n(void* port_buffer, jack_nframes_t event_index, jack_nframes_t nframes);
							   
/** Get the number of events that could not be written to @a port_buffer.
 *
 * This function returning a non-zero value implies @a port_buffer is full.
 * Currently the only way this can happen is if events are lost on port mixdown.
 *
 * @param port_buffer Port to receive count for.
 * @param nframes Number of valid frames this cycle.
 * @returns Number of events that could not be written to @a port_buffer.
 */
jack_nframes_t jack_midi_get_lost_event_count(void* port_buffer, jack_nframes_t nframes);


#ifdef __cplusplus
}
#endif


#endif /* __JACK_MIDIPORT_H */


