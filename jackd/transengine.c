/*
    JACK transport engine -- runs in the server process.

    Copyright (C) 2001-2003 Paul Davis
    Copyright (C) 2003 Jack O'Quin
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public License
    as published by the Free Software Foundation; either version 2.1
    of the License, or (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <config.h>
#include <errno.h>
#include <jack/internal.h>
#include <jack/engine.h>
#include "transengine.h"


/*********************** driver callbacks ***********************/

int
jack_set_sample_rate (jack_engine_t *engine, jack_nframes_t nframes)
{
	jack_control_t *ectl = engine->control;

	ectl->current_time.frame_rate = nframes;
	ectl->pending_time.frame_rate = nframes;
	return 0;
}

void 
jack_transport_cycle_start (jack_engine_t *engine, jack_time_t time)
{
	engine->control->current_time.guard_usecs =
		engine->control->current_time.usecs = time;
}


/********************* RPC request handlers *********************/

/* for SetSyncClient */
int
jack_set_sync_client (jack_engine_t *engine, jack_client_id_t client)
{
	int ret;
	jack_client_internal_t *clintl;

	jack_lock_graph (engine);

	clintl = jack_client_internal_by_id (engine, client);

	if (clintl) {
		clintl->control->sync_ready = 0;
		engine->control->sync_clients++;
		ret = 0;
	}  else
		ret = EINVAL;

	jack_unlock_graph (engine);

	return ret;
}

/* for ResetTimeBaseClient */
int
jack_timebase_reset (jack_engine_t *engine, jack_client_id_t client)
{
	int ret;
	struct _jack_client_internal *clint;
	jack_control_t *ectl = engine->control;

	jack_lock_graph (engine);

	clint = jack_client_internal_by_id (engine, client);
	if (clint && (clint == engine->timebase_client)) {
		clint->control->is_timebase = 0;
		engine->timebase_client = NULL;
		ectl->pending_time.valid = 0;
		ret = 0;
	}  else
		ret = EINVAL;

	jack_unlock_graph (engine);

	return ret;
}

/* for SetTimeBaseClient */
int
jack_timebase_set (jack_engine_t *engine,
		   jack_client_id_t client, int conditional)
{
	int ret = 0;
	struct _jack_client_internal *clint;

	jack_lock_graph (engine);

	clint = jack_client_internal_by_id (engine, client);

	if (conditional && engine->timebase_client) {

		/* see if timebase master is someone else */
		if (clint && (clint != engine->timebase_client))
			ret = EBUSY;

	} else {

		if (clint) {
			if (engine->timebase_client)
				engine->timebase_client->
					control->is_timebase = 0;
			engine->timebase_client = clint;
			clint->control->is_timebase = 1;
		}  else
			ret = EINVAL;
	}

	jack_unlock_graph (engine);

	return ret;
}


/******************** engine.c subroutines ********************/

/* start polling slow-sync clients */
void
jack_start_sync_poll(jack_engine_t *engine)
{
	/* precondition: caller holds the graph lock. */
	jack_control_t *ectl = engine->control;
	JSList *node;
	long sync_count = 0;		/* number of slow-sync clients */

	for (node = engine->clients; node; node = jack_slist_next (node)) {
		jack_client_internal_t *clintl =
			(jack_client_internal_t *) node->data;
		if (clintl->control->sync_cb) {
			clintl->control->sync_ready = 0;
			sync_count++;
		}
	}

	ectl->sync_remain = ectl->sync_clients = sync_count;
	ectl->sync_cycle = 0;
}

/* when timebase master exits the graph */
void
jack_timebase_exit (jack_engine_t *engine)
{
	jack_control_t *ectl = engine->control;

	engine->timebase_client->control->is_timebase = 0;
	engine->timebase_client = NULL;
	ectl->current_time.valid = 0;
	ectl->pending_time.valid = 0;
}

/* engine initialization */
void
jack_timebase_init (jack_engine_t *engine)
{
	jack_control_t *ectl = engine->control;

	engine->timebase_client = NULL;
	ectl->transport_state = JackTransportStopped;
	ectl->transport_cmd = TransportCommandNone;
	memset (&ectl->current_time, 0, sizeof(ectl->current_time));
	memset (&ectl->pending_time, 0, sizeof(ectl->pending_time));
	memset (&ectl->request_time, 0, sizeof(ectl->request_time));
	ectl->new_pos = 0;
	ectl->sync_remain = 0;
	ectl->sync_cycle = 0;
	ectl->sync_clients = 0;
}

/* This runs at the end of every process cycle.  It determines the
 * transport parameters for the next cycle.
 */
void
jack_transport_cycle_end (jack_engine_t *engine)
{
	/* precondition: caller holds the graph lock. */
	jack_control_t *ectl = engine->control;
	transport_command_t cmd;	/* latest transport command */

	/* update timebase, if needed */
	if ((engine->timebase_client == NULL) &&
	    (ectl->transport_state == JackTransportRolling)) {
		ectl->pending_time.frame =
			ectl->current_time.frame + ectl->buffer_size;
	} 

	/* Handle latest asynchronous requests from the last cycle.
	 *
	 * This should ideally use an atomic swap, since commands can
	 * arrive at any time.  There is a small timing window during
	 * which a request could be ignored inadvertently.  Since
	 * another could have arrived in the previous moment and
	 * replaced it anyway, we won't bother with <asm/atomic.h>.
	 */
	cmd = ectl->transport_cmd;
	ectl->transport_cmd = TransportCommandNone;

	if (ectl->request_time.usecs) {
		/* request_time could change during this copy */
		jack_transport_copy_position(&ectl->request_time,
					     &ectl->pending_time);
		ectl->request_time.usecs = 0; /* empty request buffer */
		ectl->new_pos = 1;
	} else
		ectl->new_pos = 0;

	/* Promote pending_time to current_time.  Maintain the usecs
	 * and frame_rate values, clients may not set them. */
	ectl->pending_time.guard_usecs = 
		ectl->pending_time.usecs = ectl->current_time.usecs;
	ectl->pending_time.frame_rate = ectl->current_time.frame_rate;
	ectl->current_time = ectl->pending_time;

	/* accumulate sync results from previous cycle */
	if (ectl->sync_remain) {
		ectl->sync_remain -= ectl->sync_cycle;
		if ((ectl->sync_remain == 0) &&
		    (ectl->transport_state == JackTransportStarting))
			ectl->transport_state = JackTransportRolling;
		ectl->sync_cycle = 0;
	}

	/* state transition switch */
	switch (ectl->transport_state) {

	case JackTransportStopped:
		if (cmd == TransportCommandPlay) {
			if (ectl->sync_clients) {
				ectl->transport_state = JackTransportStarting;
				jack_start_sync_poll(engine);
			} else
				ectl->transport_state = JackTransportRolling;
		}
		break;

	case JackTransportStarting:
	case JackTransportRolling:
		if (cmd == TransportCommandStop) {
			ectl->transport_state = JackTransportStopped;
			ectl->sync_remain = 0;	/* halt polling */
		} else if (ectl->new_pos) {
			if (ectl->sync_clients) {
				ectl->transport_state = JackTransportStarting;
				jack_start_sync_poll(engine);
			}
			else
				ectl->transport_state = JackTransportRolling;
		}
		break;

	default:
		jack_error ("invalid JACK transport state: %d",
			    ectl->transport_state);
	}
	return;
}
