/*
    Copyright (C) 2002 Stefan Kost

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    $Id$
*/

#include <math.h>
#include <stdio.h>
#include <memory.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>

#include <jack/solaris_driver.h>
#include <jack/types.h>
#include <jack/internal.h>
#include <jack/engine.h>
#include <jack/cycles.h>

extern void store_work_time (int);

extern void store_wait_time (int);
extern void show_wait_times ();
extern void show_work_times ();

//== instance callbacks ========================================================

static void
solaris_driver_attach (solaris_driver_t *driver, jack_engine_t *engine)
{
}

static void
solaris_driver_detach (solaris_driver_t *driver, jack_engine_t *engine)
{
}

static jack_nframes_t 
solaris_driver_wait (solaris_driver_t *driver, int extra_fd, int *status, float *delayed_usecs)
{
}

static int
solaris_driver_process (solaris_driver_t *driver, jack_nframes_t nframes)
{
}

static int
solaris_driver_audio_start (solaris_driver_t *driver)
{
}

static int
solaris_driver_audio_stop (solaris_driver_t *driver)
{
}

//== instance creation/destruction =============================================

/** create a new driver instance
 */
static jack_driver_t *
solaris_driver_new (char *name, 
		 jack_nframes_t frames_per_cycle,
		 jack_nframes_t user_nperiods,
		 jack_nframes_t rate,
		 int capturing,
		 int playing,
		 DitherAlgorithm dither)
{
	solaris_driver_t *driver;

	printf ("creating solaris driver ... %lu|%lu|%lu\n", 
			frames_per_cycle, user_nperiods, rate);

	driver = (solaris_driver_t *) calloc (1, sizeof (solaris_driver_t));

	jack_driver_init ((jack_driver_t *) driver);

	driver->attach = (JackDriverAttachFunction) solaris_driver_attach;
	driver->detach = (JackDriverDetachFunction) solaris_driver_detach;
	driver->wait = (JackDriverWaitFunction) solaris_driver_wait;
	driver->process = (JackDriverProcessFunction) solaris_driver_process;
	driver->start = (JackDriverStartFunction) solaris_driver_audio_start;
	driver->stop = (JackDriverStopFunction) solaris_driver_audio_stop;


	return((jack_driver_t *) driver);
}

/** free all memory allocated by a driver instance
 */
static void
solaris_driver_delete (solaris_driver_t *driver)

{
	free (driver);
}

//== driver "plugin" interface =================================================

static void
solaris_usage ()
{
	fprintf (stderr, "\

solaris PCM driver args: 
    -r sample-rate (default: 48kHz)
    -p frames-per-period (default: 1024)
    -n periods-per-hardware-buffer (default: 2)
    -D (duplex, default: yes)
    -C (capture, default: duplex)
    -P (playback, default: duplex)
    -z[r|t|s|-] (dither, rect|tri|shaped|off, default: off)
");
}

jack_driver_t *
driver_initialize (int argc, char **argv)
{
	jack_nframes_t srate = 48000;
	jack_nframes_t frames_per_interrupt = 1024;
	unsigned long user_nperiods = 2;
	int capture = FALSE;
	int playback = FALSE;
	DitherAlgorithm dither = None;
	int i;

	/* grrrr ... getopt() cannot be called in more than one "loop"
	   per process instance. ridiculous, but true. why isn't there
	   a getopt_reinitialize() function?
	*/

	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
			case 'D':
				capture = TRUE;
				playback = TRUE;
				break;

			case 'C':
				capture = TRUE;
				break;

			case 'P':
				playback = TRUE;
				break;

			case 'n':
				user_nperiods = atoi (argv[i+1]);
				i++;
				break;
				
			case 'r':
				srate = atoi (argv[i+1]);
				i++;
				break;
				
			case 'p':
				frames_per_interrupt = atoi (argv[i+1]);
				i++;
				break;
				
			case 'z':
				switch (argv[i][2]) {
					case '-':
					dither = None;
					break;

					case 'r':
					dither = Rectangular;
					break;

					case 's':
					dither = Shaped;
					break;

					case 't':
					default:
					dither = Triangular;
					break;
				}
				break;
				
			default:
				alsa_usage ();
				return NULL;
			}
		} else {
			alsa_usage ();
			return NULL;
		}
	}

	/* duplex is the default */

	if (!capture && !playback) {
		capture = TRUE;
		playback = TRUE;
	}

	return solaris_driver_new ("solaris_pcm", frames_per_interrupt, 
				user_nperiods, srate, capture, playback, dither);
}

void
driver_finish (jack_driver_t *driver)
{
	solaris_driver_delete ((solaris_driver_t *) driver);
}
