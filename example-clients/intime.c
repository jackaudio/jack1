/*
 *  intime.c -- JACK internal timebase master example client.
 *
 *  To run: first start `jackd', then `jack_load intime intime 6/8,180bpm'.
 */

/*  Copyright (C) 2003 Jack O'Quin.
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
#include <string.h>
#include <jack/jack.h>

/* Time and tempo variables, global to the entire transport timeline.
 * There is no attempt to keep a true tempo map.  The default time
 * signature is "march time": 4/4, 120bpm
 */
float time_beats_per_bar = 4.0;
float time_beat_type = 4.0;
double time_ticks_per_beat = 1920.0;
double time_beats_per_minute = 120.0;

/* BBT timebase callback.
 *
 * Runs in the process thread.  Realtime, must not wait.
 */
void 
timebbt (jack_transport_state_t state, jack_nframes_t nframes, 
	 jack_position_t *pos, int new_pos, void *arg)
{
	double min;			/* minutes since frame 0 */
	long abs_tick;			/* ticks since frame 0 */
	long abs_beat;			/* beats since frame 0 */

	if (new_pos) {

		pos->valid = JackPositionBBT;
		pos->beats_per_bar = time_beats_per_bar;
		pos->beat_type = time_beat_type;
		pos->ticks_per_beat = time_ticks_per_beat;
		pos->beats_per_minute = time_beats_per_minute;

		/* Compute BBT info from frame number.  This is
		 * relatively simple here, but would become complex if
		 * we supported tempo or time signature changes at
		 * specific locations in the transport timeline.  I
		 * make no claims for the numerical accuracy or
		 * efficiency of these calculations. */

		min = pos->frame / ((double) pos->frame_rate * 60.0);
		abs_tick = min * pos->beats_per_minute * pos->ticks_per_beat;
		abs_beat = abs_tick / pos->ticks_per_beat;

		pos->bar = abs_beat / pos->beats_per_bar;
		pos->beat = abs_beat - (pos->bar * pos->beats_per_bar) + 1;
		pos->tick = abs_tick - (abs_beat * pos->ticks_per_beat);
		pos->bar_start_tick = pos->bar * pos->beats_per_bar *
			pos->ticks_per_beat;
		pos->bar++;		/* adjust start to bar 1 */

		/* some debug code... */
		fprintf(stderr, "\nnew position: %" PRIu32 "\tBBT: %3"
			PRIi32 "|%" PRIi32 "|%04" PRIi32 "\n",
			pos->frame, pos->bar, pos->beat, pos->tick);

	} else {

		/* Compute BBT info based on previous period. */
		pos->tick += (nframes * pos->ticks_per_beat *
			      pos->beats_per_minute / (pos->frame_rate * 60));

		while (pos->tick >= pos->ticks_per_beat) {
			pos->tick -= pos->ticks_per_beat;
			if (++pos->beat > pos->beats_per_bar) {
				pos->beat = 1;
				++pos->bar;
				pos->bar_start_tick += (pos->beats_per_bar *
							pos->ticks_per_beat);
			}
		}
	}
}

/* experimental timecode callback
 *
 * Fill in extended timecode fields using the trivial assumption that
 * we are running at nominal speed, hence with no drift.
 *
 * It would probably be faster to compute frame_time without the
 * conditional expression.  But, this demonstrates the invariant:
 * next_time[i] == frame_time[i+1], unless a reposition occurs.
 *
 * Runs in the process thread.  Realtime, must not wait.
 */
void 
timecode (jack_transport_state_t state, jack_nframes_t nframes, 
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
jack_initialize (jack_client_t *client, const char *load_init)
{
	JackTimebaseCallback callback = timebbt;

	int rc = sscanf(load_init, " %f/%f, %lf bpm ", &time_beats_per_bar,
			&time_beat_type, &time_beats_per_minute);

	if (rc > 0) {
		fprintf (stderr, "counting %.1f/%.1f at %.2f bpm\n",
			 time_beats_per_bar, time_beat_type,
			 time_beats_per_minute);
	} else {
		int len = strlen(load_init);
		if ((len > 0) && (strncmp(load_init, "timecode", len) == 0))
			callback = timecode;
	}

	if (jack_set_timebase_callback(client, 0, callback, NULL) != 0) {
		fprintf (stderr, "Unable to take over timebase.\n");
		return 1;		/* terminate */
	}

	fprintf (stderr, "Internal timebase master defined.\n");
	jack_activate (client);
	return 0;			/* success */
}

/* before unloading */
void
jack_finish (void *arg)
{
	fprintf (stderr, "Internal timebase client exiting.\n");
}
