/*
    JACK transport client interface -- runs in the client process.

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
    
    You should have received a copy of the GNU Lesser General Public
    License along with this program; if not, write to the Free
    Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
    02111-1307, USA.
*/

#include <config.h>
#include <errno.h>
#include <math.h>
#include <jack/internal.h>
#include "local.h"


/********************* Internal functions *********************/

static inline void
jack_read_frame_time (const jack_client_t *client, jack_frame_timer_t *copy)
{
	int tries = 0;

	do {
		/* throttle the busy wait if we don't get 
		   the answer very quickly.
		*/

		if (tries > 10) {
			usleep (20);
			tries = 0;
		}

		*copy = client->engine->frame_timer;

		tries++;

	} while (copy->guard1 != copy->guard2);
}

/* copy a JACK transport position structure (thread-safe) */
void
jack_transport_copy_position (jack_position_t *from, jack_position_t *to)
{
	int tries = 0;
	long timeout = 1000000;

	do {
		/* throttle the busy wait if we don't get the answer
		 * very quickly. */
		if (tries > 10) {
			usleep (20);
			tries = 0;

			/* debug code to avoid system hangs... */
			if (--timeout == 0) {
				jack_error("infinte loop copying position");
				abort();
			}
		}
		*to = *from;
		tries++;

	} while (to->usecs != to->guard_usecs);
}

static inline int
jack_transport_request_new_pos (jack_client_t *client, jack_position_t *pos)
{
	jack_control_t *eng = client->engine;

	//JOQ: check validity of input

	/* carefully copy requested postion into shared memory */
	pos->guard_usecs = pos->usecs = jack_get_microseconds();
	jack_transport_copy_position (pos, &eng->request_time);
	
	return 0;
}


/******************** Callback invocations ********************/

void
jack_call_sync_client (jack_client_t *client)
{
	jack_client_control_t *control = client->control;
	jack_control_t *eng = client->engine;

	if (eng->new_pos || !control->sync_ready) {

		if (control->sync_cb (eng->transport_state,
				      &eng->current_time,
				      control->sync_arg)) {

			control->sync_ready = 1;
			eng->sync_cycle--;
		}
	}
}

void
jack_call_timebase_master (jack_client_t *client)
{
	jack_client_control_t *control = client->control;
	jack_control_t *eng = client->engine;
	int new_pos = eng->new_pos;

	/* make sure we're still the master */
	if (control->is_timebase) { 

		if (client->new_timebase) {	/* first callback? */
			client->new_timebase = 0;
			new_pos = 1;
		}

		if ((eng->transport_state == JackTransportRolling) ||
		    new_pos) {

			control->timebase_cb (eng->transport_state,
					      control->nframes,
					      &eng->pending_time,
					      new_pos,
					      control->timebase_arg);
		}

	} else {

		/* another master took over, so resign */
		client->new_timebase = 0;
		control->timebase_cb = NULL;
		control->timebase_arg = NULL;
	}
}


/************************* API functions *************************/

jack_nframes_t
jack_frames_since_cycle_start (const jack_client_t *client)
{
	float usecs;
	jack_control_t *eng = client->engine;

	usecs = jack_get_microseconds() - eng->current_time.usecs;
	return (jack_nframes_t) floor ((((float) eng->current_time.frame_rate)
					/ 1000000.0f) * usecs);
}

jack_nframes_t
jack_frame_time (const jack_client_t *client)
{
	jack_frame_timer_t current;
	float usecs;
	jack_nframes_t elapsed;
	jack_control_t *eng = client->engine;

	jack_read_frame_time (client, &current);
	
	usecs = jack_get_microseconds() - current.stamp;
	elapsed = (jack_nframes_t)
		floor ((((float) eng->current_time.frame_rate)
			/ 1000000.0f) * usecs);
	
	return current.frames + elapsed;
}

unsigned long 
jack_get_sample_rate (jack_client_t *client)
{
	return client->engine->current_time.frame_rate;
}

int
jack_set_sample_rate_callback (jack_client_t *client,
			       JackSampleRateCallback callback, void *arg)
{
	if (client->control->active) {
		jack_error ("You cannot set callbacks on an active client.");
		return -1;
	}
	client->control->srate_arg = arg;
	client->control->srate = callback;

	/* Now invoke it */

	callback (client->engine->current_time.frame_rate, arg);

	return 0;
}

int  
jack_release_timebase (jack_client_t *client)
{
	int rc;
	jack_request_t req;
	jack_client_control_t *ctl = client->control;

	req.type = ResetTimeBaseClient;
	req.x.client_id = ctl->id;

	rc = jack_client_deliver_request (client, &req);
	if (rc == 0) {
		client->new_timebase = 0;
		ctl->timebase_cb = NULL;
		ctl->timebase_arg = NULL;
	}
	return rc;
}

int  
jack_set_sync_callback (jack_client_t *client,
			JackSyncCallback sync_callback, void *arg)
{
	jack_client_control_t *ctl = client->control;
	jack_request_t req;
	int rc;

	req.type = SetSyncClient;
	req.x.client_id = ctl->id;

	rc = jack_client_deliver_request (client, &req);
	if (rc == 0) {
		ctl->sync_cb = sync_callback;
		ctl->sync_arg = arg;
	}
	return rc;
}

int  
jack_set_sync_timeout (jack_client_t *client, jack_nframes_t timeout)
{
	return ENOSYS;			/* this is a stub */
}

int  
jack_set_timebase_callback (jack_client_t *client, int conditional,
			    JackTimebaseCallback timebase_cb, void *arg)
{
	int rc;
	jack_request_t req;
	jack_client_control_t *ctl = client->control;

	req.type = SetTimeBaseClient;
	req.x.timebase.client_id = ctl->id;
	req.x.timebase.conditional = conditional;

	rc = jack_client_deliver_request (client, &req);
	if (rc == 0) {
		client->new_timebase = 1;
		ctl->timebase_arg = arg;
		ctl->timebase_cb = timebase_cb;
	}
	return rc;
}

int
jack_transport_goto_frame (jack_client_t *client, jack_nframes_t frame)
{
	jack_position_t pos;

	pos.frame = frame;
	pos.valid = 0;
	return jack_transport_request_new_pos (client, &pos);
}

jack_transport_state_t 
jack_transport_query (jack_client_t *client, jack_position_t *pos)
{
	jack_control_t *eng = client->engine;

	/* the guarded copy makes this function work in any thread */
	jack_transport_copy_position (&eng->current_time, pos);
	return eng->transport_state;
}

int
jack_transport_reposition (jack_client_t *client, jack_position_t *pos)
{
	/* copy the input, so we don't modify the input argument */
	jack_position_t tmp = *pos;

	return jack_transport_request_new_pos (client, &tmp);
}

void  
jack_transport_start (jack_client_t *client)
{
	client->engine->transport_cmd = TransportCommandPlay;
}

void
jack_transport_stop (jack_client_t *client)
{
	client->engine->transport_cmd = TransportCommandStop;
}


#ifdef OLD_TRANSPORT

/************* Compatibility with old transport API. *************/

int
jack_engine_takeover_timebase (jack_client_t *client)
{
	jack_request_t req;

	req.type = SetTimeBaseClient;
	req.x.timebase.client_id = client->control->id;
	req.x.timebase.conditional = 0;

	return jack_client_deliver_request (client, &req);
}	

void
jack_get_transport_info (jack_client_t *client,
			 jack_transport_info_t *info)
{
	jack_control_t *eng = client->engine;

	/* check that this is the process thread */
	if (client->thread_id != pthread_self()) {
		jack_error("Invalid thread for jack_get_transport_info().");
		abort();		/* kill this client */
	}

	info->usecs = eng->current_time.usecs;
	info->frame_rate = eng->current_time.frame_rate;
	info->transport_state = eng->transport_state;
	info->frame = eng->current_time.frame;
	info->valid = (eng->current_time.valid |
		       JackTransportState | JackTransportPosition);

	if (info->valid & JackPositionBBT) {
		info->bar = eng->current_time.bar;
		info->beat = eng->current_time.beat;
		info->tick = eng->current_time.tick;
		info->bar_start_tick = eng->current_time.bar_start_tick;
		info->beats_per_bar = eng->current_time.beats_per_bar;
		info->beat_type = eng->current_time.beat_type;
		info->ticks_per_beat = eng->current_time.ticks_per_beat;
		info->beats_per_minute = eng->current_time.beats_per_minute;
	}
}

static int first_error = 1;

void
jack_set_transport_info (jack_client_t *client,
			 jack_transport_info_t *info)
{
	jack_control_t *eng = client->engine;

	if (!client->control->is_timebase) { /* not timebase master? */
		if (first_error)
			jack_error ("Called jack_set_transport_info(), but not timebase master.");
		first_error = 0;

		/* JOQ: I would prefer to ignore this request, but if
		 * I do, it breaks ardour 0.9-beta2.  So, let's allow
		 * it for now. */
		// return;
	}

	/* check that this is the process thread */
	if (client->thread_id != pthread_self()) {
		jack_error ("Invalid thread for jack_set_transport_info().");
		abort();		/* kill this client */
	}

	/* is there a new state? */
	if ((info->valid & JackTransportState) &&
	    (info->transport_state != eng->transport_state)) {
		if (info->transport_state == JackTransportStopped)
			eng->transport_cmd = TransportCommandStop;
		else if (info->transport_state == JackTransportRolling)
			eng->transport_cmd = TransportCommandPlay;
		/* silently ignore anything else */
	}

	if (info->valid & JackTransportPosition)
		eng->pending_time.frame = info->frame;
	else
		eng->pending_time.frame = eng->current_time.frame;

	eng->pending_time.valid = (info->valid & JackTransportBBT);

	if (info->valid & JackTransportBBT) {
		eng->pending_time.bar = info->bar;
		eng->pending_time.beat = info->beat;
		eng->pending_time.tick = info->tick;
		eng->pending_time.bar_start_tick = info->bar_start_tick;
		eng->pending_time.beats_per_bar = info->beats_per_bar;
		eng->pending_time.beat_type = info->beat_type;
		eng->pending_time.ticks_per_beat = info->ticks_per_beat;
		eng->pending_time.beats_per_minute = info->beats_per_minute;
	}
}	

#endif /* OLD_TRANSPORT */
