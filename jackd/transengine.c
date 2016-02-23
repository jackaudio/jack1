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
#include <string.h>
#include <stdio.h>

#include "internal.h"
#include "engine.h"
#include "messagebuffer.h"
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
	if (engine->control->transport_state == JackTransportRolling) {
		engine->control->transport_state = JackTransportStarting;
		VERBOSE (engine, "force transport state to Starting");
	}

	VERBOSE (engine, "polling sync client %s", client->control->name);
}

/* stop polling a specific slow-sync client
 *
 *   precondition: caller holds the graph lock. */
static inline void
jack_sync_poll_deactivate (jack_engine_t *engine,
			   jack_client_internal_t *client)
{
	if (client->control->sync_poll) {
		client->control->sync_poll = 0;
		client->control->sync_new = 0;
		engine->control->sync_remain--;
		VERBOSE (engine, "sync poll interrupted for client %s", client->control->name);
	}
	client->control->active_slowsync = 0;
	engine->control->sync_clients--;
	assert (engine->control->sync_clients >= 0);
}

/* stop polling all the slow-sync clients
 *
 *   precondition: caller holds the graph lock. */
static void
jack_sync_poll_stop (jack_engine_t *engine)
{
	JSList *node;
	long poll_count = 0;            /* count sync_poll clients */

	for (node = engine->clients; node; node = jack_slist_next (node)) {
		jack_client_internal_t *client =
			(jack_client_internal_t*)node->data;
		if (client->control->active_slowsync &&
		    client->control->sync_poll) {
			client->control->sync_poll = 0;
			poll_count++;
		}
	}

	//JOQ: check invariant for debugging...
	assert (poll_count == engine->control->sync_remain);
	VERBOSE (engine,
		 "sync poll halted with %" PRIu32
		 " clients and %8.6f secs remaining",
		 engine->control->sync_remain,
		 (double)(engine->control->sync_time_left / 1000000.0));
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
	long sync_count = 0;            /* count slow-sync clients */

	for (node = engine->clients; node; node = jack_slist_next (node)) {
		jack_client_internal_t *client =
			(jack_client_internal_t*)node->data;
		if (client->control->active_slowsync) {
			client->control->sync_poll = 1;
			sync_count++;
		}
	}

	//JOQ: check invariant for debugging...
	assert (sync_count == engine->control->sync_clients);
	engine->control->sync_remain = sync_count;
	engine->control->sync_time_left = engine->control->sync_timeout;
	VERBOSE (engine, "transport Starting, sync poll of %" PRIu32
		 " clients for %8.6f secs", engine->control->sync_remain,
		 (double)(engine->control->sync_time_left / 1000000.0));
}

/* check for sync timeout */
static inline int
jack_sync_timeout (jack_engine_t *engine)
{
	jack_control_t *ectl = engine->control;
	jack_time_t buf_usecs =
		((ectl->buffer_size * (jack_time_t)1000000) /
		 ectl->current_time.frame_rate);

	/* compare carefully, jack_time_t is unsigned */
	if (ectl->sync_time_left > buf_usecs) {
		ectl->sync_time_left -= buf_usecs;
		return FALSE;
	}

	/* timed out */
	VERBOSE (engine, "transport sync timeout");
	ectl->sync_time_left = 0;
	return TRUE;
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
jack_timebase_reset (jack_engine_t *engine, jack_uuid_t client_id)
{
	int ret;
	struct _jack_client_internal *client;
	jack_control_t *ectl = engine->control;

	jack_lock_graph (engine);

	client = jack_client_internal_by_id (engine, client_id);
	if (client && (client == engine->timebase_client)) {
		client->control->is_timebase = 0;
		client->control->timebase_new = 0;
		engine->timebase_client = NULL;
		ectl->pending_time.valid = 0;
		VERBOSE (engine, "%s resigned as timebase master",
			 client->control->name);
		ret = 0;
	} else {
		ret = EINVAL;
	}

	jack_unlock_graph (engine);

	return ret;
}

/* on SetTimeBaseClient request */
int
jack_timebase_set (jack_engine_t *engine,
		   jack_uuid_t client_id, int conditional)
{
	int ret = 0;
	struct _jack_client_internal *client;

	jack_lock_graph (engine);

	client = jack_client_internal_by_id (engine, client_id);

	if (client == NULL) {
		VERBOSE (engine, " %" PRIu32 " no longer exists", client_id);
		jack_unlock_graph (engine);
		return EINVAL;
	}

	if (conditional && engine->timebase_client) {

		/* see if timebase master is someone else */
		if (client != engine->timebase_client) {
			VERBOSE (engine, "conditional timebase for %s failed",
				 client->control->name);
			VERBOSE (engine, " %s is already the master",
				 engine->timebase_client->control->name);
			ret = EBUSY;
		} else {
			VERBOSE (engine, " %s was already timebase master:",
				 client->control->name);
		}

	} else {

		if (engine->timebase_client) {
			engine->timebase_client->control->is_timebase = 0;
			engine->timebase_client->control->timebase_new = 0;
		}
		engine->timebase_client = client;
		client->control->is_timebase = 1;
		if (client->control->active) {
			client->control->timebase_new = 1;
		}
		VERBOSE (engine, "new timebase master: %s",
			 client->control->name);
	}

	jack_unlock_graph (engine);

	return ret;
}

/* for client activation
 *
 *   precondition: caller holds the graph lock. */
void
jack_transport_activate (jack_engine_t *engine, jack_client_internal_t *client)
{
	if (client->control->is_slowsync) {
		assert (!client->control->active_slowsync);
		client->control->active_slowsync = 1;
		engine->control->sync_clients++;
		jack_sync_poll_new (engine, client);
	}

	if (client->control->is_timebase) {
		client->control->timebase_new = 1;
	}
}

/* for engine initialization */
void
jack_transport_init (jack_engine_t *engine)
{
	jack_control_t *ectl = engine->control;

	engine->timebase_client = NULL;
	ectl->transport_state = JackTransportStopped;
	ectl->transport_cmd = TransportCommandStop;
	ectl->previous_cmd = TransportCommandStop;
	memset (&ectl->current_time, 0, sizeof(ectl->current_time));
	memset (&ectl->pending_time, 0, sizeof(ectl->pending_time));
	memset (&ectl->request_time, 0, sizeof(ectl->request_time));
	ectl->prev_request = 0;
	ectl->seq_number = 1;           /* can't start at 0 */
	ectl->new_pos = 0;
	ectl->pending_pos = 0;
	ectl->pending_frame = 0;
	ectl->sync_clients = 0;
	ectl->sync_remain = 0;
	ectl->sync_timeout = 2000000;   /* 2 second default */
	ectl->sync_time_left = 0;
}

/* when any client exits the graph (either dead or not active)
 *
 * precondition: caller holds the graph lock */
void
jack_transport_client_exit (jack_engine_t *engine,
			    jack_client_internal_t *client)
{
	if (client == engine->timebase_client) {
		if (client->control->dead) {
			engine->timebase_client->control->is_timebase = 0;
			engine->timebase_client->control->timebase_new = 0;
			engine->timebase_client = NULL;
			VERBOSE (engine, "timebase master exit");
		}
		engine->control->current_time.valid = 0;
		engine->control->pending_time.valid = 0;
	}

	if (client->control->is_slowsync) {
		if (client->control->active_slowsync) {
			jack_sync_poll_deactivate (engine, client);
		}
		if (client->control->dead) {
			client->control->is_slowsync = 0;
		}
	}
}

/* when a new client is being created */
void
jack_transport_client_new (jack_client_internal_t *client)
{
	client->control->is_timebase = 0;
	client->control->timebase_new = 0;
	client->control->is_slowsync = 0;
	client->control->active_slowsync = 0;
	client->control->sync_poll = 0;
	client->control->sync_new = 0;

	client->control->sync_cb_cbset = FALSE;
	client->control->timebase_cb_cbset = FALSE;

#if 0
	if (client->control->type != ClientExternal) {
		client->sync_cb = NULL;
		client->sync_arg = NULL;
		client->timebase_cb = NULL;
		client->timebase_arg = NULL;
	}
#endif
}

/* on ResetSyncClient request */
int
jack_transport_client_reset_sync (jack_engine_t *engine,
				  jack_uuid_t client_id)
{
	int ret;
	jack_client_internal_t *client;

	jack_lock_graph (engine);

	client = jack_client_internal_by_id (engine, client_id);

	if (client && (client->control->is_slowsync)) {
		if (client->control->active_slowsync) {
			jack_sync_poll_deactivate (engine, client);
		}
		client->control->is_slowsync = 0;
		ret = 0;
	} else {
		ret = EINVAL;
	}

	jack_unlock_graph (engine);

	return ret;
}

/* on SetSyncClient request */
int
jack_transport_client_set_sync (jack_engine_t *engine,
				jack_uuid_t client_id)
{
	int ret;
	jack_client_internal_t *client;

	DEBUG ("set sync client");

	/* The process cycle runs with this lock. */
	jack_lock_graph (engine);

	DEBUG ("got write lock");

	client = jack_client_internal_by_id (engine, client_id);

	DEBUG ("client was %p");

	if (client) {
		if (!client->control->is_slowsync) {
			client->control->is_slowsync = 1;
			if (client->control->active) {
				client->control->active_slowsync = 1;
				engine->control->sync_clients++;
			}
		}

		/* force poll of the new slow-sync client, if active */
		if (client->control->active_slowsync) {
			DEBUG ("sync poll new");
			jack_sync_poll_new (engine, client);
		}
		ret = 0;
	} else {
		ret = EINVAL;
	}

	DEBUG ("unlocking write lock for set_sync");
	jack_unlock_graph (engine);


	return ret;
}

/* at process cycle end, set transport parameters for the next cycle
 *
 * precondition: caller holds the graph lock.
 */
void
jack_transport_cycle_end (jack_engine_t *engine)
{
	jack_control_t *ectl = engine->control;
	transport_command_t cmd;        /* latest transport command */

	/* Promote pending_time to current_time.  Maintain the usecs,
	 * frame_rate and frame values, clients may not set them. */

	ectl->pending_time.usecs = ectl->current_time.usecs;
	ectl->pending_time.frame_rate = ectl->current_time.frame_rate;
	ectl->pending_time.frame = ectl->pending_frame;
	ectl->current_time = ectl->pending_time;
	ectl->new_pos = ectl->pending_pos;

	/* check sync results from previous cycle */
	if (ectl->transport_state == JackTransportStarting) {
		if ((ectl->sync_remain == 0) ||
		    (jack_sync_timeout (engine))) {
			ectl->transport_state = JackTransportRolling;
			VERBOSE (engine, "transport Rolling, %8.6f sec"
				 " left for poll",
				 (double)(ectl->sync_time_left / 1000000.0));
		}
	}

	/* Handle any new transport command from the last cycle. */
	cmd = ectl->transport_cmd;
	if (cmd != ectl->previous_cmd) {
		ectl->previous_cmd = cmd;
		VERBOSE (engine, "transport command: %s",
			 (cmd == TransportCommandStart ? "START" : "STOP"));
	} else {
		cmd = TransportCommandNone;
	}

	/* state transition switch */

	switch (ectl->transport_state) {

	case JackTransportStopped:
		if (cmd == TransportCommandStart) {
			if (ectl->sync_clients) {
				ectl->transport_state = JackTransportStarting;
				jack_sync_poll_start (engine);
			} else {
				ectl->transport_state = JackTransportRolling;
				VERBOSE (engine, "transport Rolling");
			}
		}
		break;

	case JackTransportStarting:
		if (cmd == TransportCommandStop) {
			ectl->transport_state = JackTransportStopped;
			VERBOSE (engine, "transport Stopped");
			if (ectl->sync_remain) {
				jack_sync_poll_stop (engine);
			}
		} else if (ectl->new_pos) {
			if (ectl->sync_clients) {
				ectl->transport_state = JackTransportStarting;
				jack_sync_poll_start (engine);
			} else {
				ectl->transport_state = JackTransportRolling;
				VERBOSE (engine, "transport Rolling");
			}
		}
		break;

	case JackTransportRolling:
		if (cmd == TransportCommandStop) {
			ectl->transport_state = JackTransportStopped;
			VERBOSE (engine, "transport Stopped");
			if (ectl->sync_remain) {
				jack_sync_poll_stop (engine);
			}
		} else if (ectl->new_pos) {
			if (ectl->sync_clients) {
				ectl->transport_state = JackTransportStarting;
				jack_sync_poll_start (engine);
			}
		}
		break;

	default:
		jack_error ("invalid JACK transport state: %d",
			    ectl->transport_state);
	}

	/* Update timebase, if needed. */
	if (ectl->transport_state == JackTransportRolling) {
		ectl->pending_time.frame =
			ectl->current_time.frame + ectl->buffer_size;
	}

	/* See if an asynchronous position request arrived during the
	 * last cycle.  The request_time could change during the
	 * guarded copy.  If so, we use the newest request. */
	ectl->pending_pos = 0;
	if (ectl->request_time.unique_1 != ectl->prev_request) {
		jack_transport_copy_position (&ectl->request_time,
					      &ectl->pending_time);
		VERBOSE (engine, "new transport position: %" PRIu32
			 ", id=0x%" PRIx64, ectl->pending_time.frame,
			 ectl->pending_time.unique_1);
		ectl->prev_request = ectl->pending_time.unique_1;
		ectl->pending_pos = 1;
	}

	/* clients can't set pending frame number, so save it here */
	ectl->pending_frame = ectl->pending_time.frame;
}

/* driver callback at start of cycle */
void
jack_transport_cycle_start (jack_engine_t *engine, jack_time_t time)
{
	engine->control->current_time.usecs = time;
}

/* on SetSyncTimeout request */
int
jack_transport_set_sync_timeout (jack_engine_t *engine,
				 jack_time_t usecs)
{
	engine->control->sync_timeout = usecs;
	VERBOSE (engine, "new sync timeout: %8.6f secs",
		 (double)(usecs / 1000000.0));
	return 0;
}
