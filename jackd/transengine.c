/*
    JACK transport engine -- runs in the server process.

    Copyright (C) 2001-2003 Paul Davis
    Copyright (C) 2003 Jack O'Quin
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.
    
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
#include <assert.h>
#include <jack/internal.h>
#include <jack/engine.h>
#include "transengine.h"


/********************** internal functions **********************/

/* initiate polling a new slow-sync client
 *
 *   precondition: caller holds the graph lock. */
static inline void
jack_sync_poll_new (jack_engine_t *engine, jack_client_internal_t *client)
{
	/* force sync_cb callback to run in its first cycle */
	engine->control->sync_time_left = engine->control->sync_timeout;
	client->control->sync_new = 1;
	if (!client->control->sync_poll) {
		client->control->sync_poll = 1;
		engine->control->sync_remain++;
	}

	// JOQ: I don't like doing this here...
	if (engine->control->transport_state == JackTransportRolling)
		engine->control->transport_state = JackTransportStarting;
}

/* stop polling a specific slow-sync client
 *
 *   precondition: caller holds the graph lock. */
static inline void
jack_sync_poll_exit (jack_engine_t *engine, jack_client_internal_t *client)
{
	if (client->control->sync_poll) {
		client->control->sync_poll = 0;
		client->control->sync_new = 0;
		engine->control->sync_remain--;
	}
	client->control->is_slowsync = 0;
	engine->control->sync_clients--;
}

/* stop polling all the slow-sync clients
 *
 *   precondition: caller holds the graph lock. */
static void
jack_sync_poll_stop (jack_engine_t *engine)
{
	JSList *node;
	long poll_count = 0;		/* count sync_poll clients */

	for (node = engine->clients; node; node = jack_slist_next (node)) {
		jack_client_internal_t *client =
			(jack_client_internal_t *) node->data;
		if (client->control->is_slowsync &&
		    client->control->sync_poll) {
			client->control->sync_poll = 0;
			poll_count++;
		}
	}

	//JOQ: check invariant for debugging...
	assert (poll_count == engine->control->sync_remain);
	engine->control->sync_remain = 0;
	engine->control->sync_time_left = 0;
}

/* start polling all the slow-sync clients
 *
 *   precondition: caller holds the graph lock. */
static void
jack_sync_poll_start (jack_engine_t *engine)
{
	JSList *node;
	long sync_count = 0;		/* count slow-sync clients */

	for (node = engine->clients; node; node = jack_slist_next (node)) {
		jack_client_internal_t *client =
			(jack_client_internal_t *) node->data;
		if (client->control->is_slowsync) {
			client->control->sync_poll = 1;
			sync_count++;
		}
	}

	//JOQ: check invariant for debugging...
	assert (sync_count == engine->control->sync_clients);
	engine->control->sync_remain = engine->control->sync_clients;
	engine->control->sync_time_left = engine->control->sync_timeout;
}

/* check for sync timeout */
static inline int
jack_sync_timeout (jack_engine_t *engine)
{
	jack_control_t *ectl = engine->control;
	jack_time_t buf_usecs =
		((ectl->buffer_size * (jack_time_t) 1000000) /
		 ectl->current_time.frame_rate);

	/* compare carefully, jack_time_t is unsigned */
	if (ectl->sync_time_left > buf_usecs) {
		ectl->sync_time_left -= buf_usecs;
		return 0;		/* continue */
	}

	/* timed out */
	ectl->sync_time_left = 0;
	return 1;
}

/**************** subroutines used by engine.c ****************/

/* driver callback */
int
jack_set_sample_rate (jack_engine_t *engine, jack_nframes_t nframes)
{
	jack_control_t *ectl = engine->control;

	ectl->current_time.frame_rate = nframes;
	ectl->pending_time.frame_rate = nframes;
	return 0;
}

/* on ResetTimeBaseClient request */
int
jack_timebase_reset (jack_engine_t *engine, jack_client_id_t client_id)
{
	int ret;
	struct _jack_client_internal *client;
	jack_control_t *ectl = engine->control;

	jack_lock_graph (engine);

	client = jack_client_internal_by_id (engine, client_id);
	if (client && (client == engine->timebase_client)) {
		client->control->is_timebase = 0;
		engine->timebase_client = NULL;
		ectl->pending_time.valid = 0;
		ret = 0;
	}  else
		ret = EINVAL;

	jack_unlock_graph (engine);

	return ret;
}

/* on SetTimeBaseClient request */
int
jack_timebase_set (jack_engine_t *engine,
		   jack_client_id_t client_id, int conditional)
{
	int ret = 0;
	struct _jack_client_internal *client;

	jack_lock_graph (engine);

	client = jack_client_internal_by_id (engine, client_id);

	if (conditional && engine->timebase_client) {

		/* see if timebase master is someone else */
		if (client && (client != engine->timebase_client))
			ret = EBUSY;

	} else {

		if (client) {
			if (engine->timebase_client)
				engine->timebase_client->
					control->is_timebase = 0;
			engine->timebase_client = client;
			client->control->is_timebase = 1;
		}  else
			ret = EINVAL;
	}

	jack_unlock_graph (engine);

	return ret;
}

/* for engine initialization */
void
jack_transport_init (jack_engine_t *engine)
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
	ectl->sync_clients = 0;
	ectl->sync_timeout = 2000000;	/* 2 second default */
	ectl->sync_time_left = 0;
}

/* when any client exits the graph
 *
 * precondition: caller holds the graph lock */
void
jack_transport_client_exit (jack_engine_t *engine,
			    jack_client_internal_t *client)
{
	if (client == engine->timebase_client) {
		engine->timebase_client->control->is_timebase = 0;
		engine->timebase_client = NULL;
		engine->control->current_time.valid = 0;
		engine->control->pending_time.valid = 0;
	}

	if (client->control->is_slowsync)
		jack_sync_poll_exit(engine, client);
}

/* when a new client is being created */
void	
jack_transport_client_new (jack_client_internal_t *client)
{
	client->control->is_timebase = 0;
	client->control->is_slowsync = 0;
	client->control->sync_poll = 0;
	client->control->sync_new = 0;
	client->control->sync_cb = NULL;
	client->control->sync_arg = NULL;
	client->control->timebase_cb = NULL;
	client->control->timebase_arg = NULL;
}

/* on ResetSyncClient request */
int
jack_transport_client_reset_sync (jack_engine_t *engine,
				  jack_client_id_t client_id)
{
	int ret;
	jack_client_internal_t *client;

	jack_lock_graph (engine);

	client = jack_client_internal_by_id (engine, client_id);

	if (client && (client->control->is_slowsync)) {
		jack_sync_poll_exit(engine, client);
		ret = 0;
	}  else
		ret = EINVAL;

	jack_unlock_graph (engine);

	return ret;
}

/* on SetSyncClient request */
int
jack_transport_client_set_sync (jack_engine_t *engine,
				jack_client_id_t client_id)
{
	int ret;
	jack_client_internal_t *client;

	// JOQ: I am assuming the process cycle is serialized with
	// respect to this lock...
	jack_lock_graph (engine);

	client = jack_client_internal_by_id (engine, client_id);

	if (client) {
		if (!client->control->is_slowsync) {
			client->control->is_slowsync = 1;
			engine->control->sync_clients++;
		}

		/* force poll of the new slow-sync client */
		jack_sync_poll_new (engine, client);
		ret = 0;
	}  else
		ret = EINVAL;

	jack_unlock_graph (engine);

	return ret;
}

/* at the end of every process cycle
 *
 * Determines the transport parameters for the following cycle.
 * precondition: caller holds the graph lock.
 */
void
jack_transport_cycle_end (jack_engine_t *engine)
{
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
	// JOQ: may be able to close the window by eliminating this
	// store, but watch out below...
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

	/* check sync results from previous cycle */
	if (ectl->transport_state == JackTransportStarting) {
		if ((ectl->sync_remain == 0) ||
		    (jack_sync_timeout(engine)))
			ectl->transport_state = JackTransportRolling;



	}

	/* state transition switch */
	switch (ectl->transport_state) {

	case JackTransportStopped:
		if (cmd == TransportCommandStart) {
			if (ectl->sync_clients) {
				ectl->transport_state = JackTransportStarting;
				jack_sync_poll_start(engine);
			} else {
				ectl->transport_state = JackTransportRolling;
			}
		}
		break;

	case JackTransportStarting:
	case JackTransportRolling:
		if (cmd == TransportCommandStop) {
			ectl->transport_state = JackTransportStopped;
			jack_sync_poll_stop(engine);
		} else if (ectl->new_pos) {
			if (ectl->sync_clients) {
				ectl->transport_state = JackTransportStarting;
				jack_sync_poll_start(engine);
			} else {
				ectl->transport_state = JackTransportRolling;
			}
		}
		break;

	default:
		jack_error ("invalid JACK transport state: %d",
			    ectl->transport_state);
	}
	return;
}

/* driver callback at start of cycle */
void 
jack_transport_cycle_start (jack_engine_t *engine, jack_time_t time)
{
	engine->control->current_time.guard_usecs =
		engine->control->current_time.usecs = time;
}

/* on SetSyncTimeout request */
int	
jack_transport_set_sync_timeout (jack_engine_t *engine,
				 jack_time_t usecs)
{
	engine->control->sync_timeout = usecs;
	return 0;
}
