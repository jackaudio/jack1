/*
    JACK transport engine -- runs in the server process.

    Copyright (C) 2001-2003 Paul Davis
    Copyright (C) 2003 Jack O'Quin
    
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

#include <config.h>
#include <jack/internal.h>
#include <jack/engine.h>
#include "transengine.h"

int
jack_set_sample_rate (jack_engine_t *engine, jack_nframes_t nframes)
{
	engine->control->current_time.frame_rate = nframes;
	engine->control->pending_time.frame_rate = nframes;
	return 0;
}

void
jack_transport_cycle_end (jack_engine_t *engine)
{
	jack_control_t *ctl = engine->control;

	/* maintain the current_time.usecs and frame_rate values,
	   since clients are not permitted to set them.
	*/
	ctl->pending_time.usecs = ctl->current_time.usecs;
	ctl->pending_time.frame_rate = ctl->current_time.frame_rate;
	ctl->current_time = ctl->pending_time;
}

void
jack_transport_reset (jack_engine_t *engine)
{
#ifdef OLD_TRANSPORT
	jack_control_t *ctl = engine->control;

	ctl->current_time.frame = 0;
	ctl->pending_time.frame = 0;
	ctl->current_time.transport_state = JackTransportStopped;
	ctl->pending_time.transport_state = JackTransportStopped;
	ctl->current_time.valid =
		JackTransportState|JackTransportPosition;
	ctl->pending_time.valid =
		JackTransportState|JackTransportPosition;
#endif /* OLD_TRANSPORT */
}
