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

typedef enum {

	JackTransportStopped,
	JackTransportRolling,
	JackTransportLooping

} jack_transport_state_t;

typedef enum {

        JackTransportState =    0x1,
        JackTransportPosition = 0x2,
        JackTransportLoop =     0x4

} jack_transport_bits_t;

typedef struct {

    jack_transport_bits_t  valid;
    jack_transport_state_t state;
    jack_nframes_t              position;
    jack_nframes_t              loop_start;
    jack_nframes_t              loop_end;

} jack_transport_info_t;

int jack_set_transport_info (jack_client_t *client,
			     jack_transport_info_t *);
int jack_get_transport_info (jack_client_t *client,
			     jack_transport_info_t *);

#ifdef __cplusplus
}
#endif

#endif /* __jack_transport_h__ */
