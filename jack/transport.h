/*
    Copyright (C) 2002 Paul Davis
    
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

#ifndef __jack_transport_h__
#define __jack_transport_h__

#ifdef __cplusplus
extern "C" {
#endif

#include <jack/types.h>

/**
 * Possible transport states.
 */
typedef enum {

	JackTransportStopped,
	JackTransportRolling,
	JackTransportLooping

} jack_transport_state_t;

/**
 * Bitfield of all possible transport info struct
 * fields.
 *
 * @see jack_transport_info_t
 */
typedef enum {

        JackTransportState =    0x1,
        JackTransportPosition = 0x2,
        JackTransportLoop =     0x4,
	JackTransportSMPTE =    0x8,
	JackTransportBBT =      0x10,

} jack_transport_bits_t;

#define EXTENDED_TIME_INFO \

/**
 * Struct for transport status information.
 */
typedef struct {
    
    /* these two cannot be set from clients: the server sets them */

    jack_nframes_t frame_rate;               // current frame rate (per second)
    jack_time_t    usecs;                    // monotonic, free-rolling

    jack_transport_bits_t  valid;            // which fields are legal to read
    jack_transport_state_t transport_state;         
    jack_nframes_t         frame;
    jack_nframes_t         loop_start;
    jack_nframes_t         loop_end;

    long           smpte_offset;             // SMPTE offset (SMPTE frame when frame = 0)
    float          smpte_frame_rate;         // 29.97, 30, 24 etc.

    int            bar;                      // current bar
    int            beat;                     // current beat-within-bar
    int            tick;                     // current tick-within-beat
    double         bar_start_tick;           // 

    float          beats_per_bar;
    float          beat_type;
    double         ticks_per_beat;
    double         beats_per_minute;

} jack_transport_info_t;

/**
 * Sets the transport state for the next engine 
 * cycle.
 *
 * The 'valid' field of the tinfo struct should contain 
 * a bitmask of all transport info fields that are set
 * in tinfo.
 *
 * @pre Caller must be the current timebase master.
 *
 */
void jack_set_transport_info (jack_client_t *client,
			      jack_transport_info_t *tinfo);
	
/**
 * Gets the current transport state. 
 *
 * On return, the 'valid' field of the tinfo struct will contain 
 * a bitmask of all transport info fields that are legal to
 * use.
 *
 */
void jack_get_transport_info (jack_client_t *client,
			      jack_transport_info_t *tinfo);

#ifdef __cplusplus
}
#endif

#endif /* __jack_transport_h__ */
