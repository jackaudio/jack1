/*
 *  intime.c -- JACK internal timebase master example client.
 *
 *  This demonstrates how to write an internal timebase master client.
 *  It fills in extended timecode fields using the trivial assumption
 *  that we are running at nominal speed, hence there is no drift.
 *
 *  To run, first start `jackd', then `jack_load intime intime'.
 *
 *  Copyright (C) 2003 Jack O'Quin.
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <jack/jack.h>


/* timebase callback
 *
 * It would probably be faster to compute frame_time without the
 * conditional expression.  But, it demonstrates the invariant:
 * next_time[i] == frame_time[i+1], unless a reposition occurs.
 *
 * Runs in the process thread.  Realtime, must not wait.
 */
void timebase(jack_transport_state_t state, jack_nframes_t nframes, 
	      jack_position_t *pos, int new_pos, void *arg)
{
	/* nominal transport speed */
	double seconds_per_frame = 1.0 / (double) pos->frame_rate;

	pos->valid = JackPositionTimecode;
	pos->frame_time = (new_pos?
			   pos->frame * seconds_per_frame:
			   pos->next_time);
	pos->next_time = (pos->frame + nframes) * seconds_per_frame;
}

/* after internal client loaded */
int
jack_initialize (jack_client_t *client, const char *data)
{
	if (jack_set_timebase_callback(client, 0, timebase, NULL) != 0) {
		fprintf (stderr, "Unable to take over timebase.\n");
		return 1;
	}

	fprintf (stderr, "Internal timebase master defined.\n");
	jack_activate (client);
	return 0;
}

/* before unloading */
void
jack_finish (void)
{
	fprintf (stderr, "Internal timebase client exiting.\n");
}
