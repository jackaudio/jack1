/* -*- mode: c; c-file-style: "linux"; -*- */
/*
    Copyright (C) 2001-2003 Paul Davis
    
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

#include <config.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <jack/internal.h>
#include <jack/driver.h>
#include <jack/engine.h>

#ifdef USE_MLOCK
#include <sys/mman.h>
#endif /* USE_MLOCK */

static int dummy_attach (jack_driver_t *drv, jack_engine_t *eng) { return 0; }
static int dummy_detach (jack_driver_t *drv, jack_engine_t *eng) { return 0; }
static int dummy_write (jack_driver_t *drv,
			jack_nframes_t nframes) { return 0; }
static int dummy_read (jack_driver_t *drv, jack_nframes_t nframes) { return 0; }
static int dummy_null_cycle (jack_driver_t *drv,
			     jack_nframes_t nframes) { return 0; }
static int dummy_bufsize (jack_driver_t *drv,
			  jack_nframes_t nframes) {return 0;}
static int dummy_stop (jack_driver_t *drv) { return 0; }
static int dummy_start (jack_driver_t *drv) { return 0; }

void
jack_driver_init (jack_driver_t *driver)
{
	memset (driver, 0, sizeof (*driver));

	driver->attach = dummy_attach;
	driver->detach = dummy_detach;
	driver->write = dummy_write;
	driver->read = dummy_read;
	driver->null_cycle = dummy_null_cycle;
	driver->bufsize = dummy_bufsize;
	driver->start = dummy_start;
	driver->stop = dummy_stop;
}



/****************************
 *** Non-Threaded Drivers ***
 ****************************/

static int dummy_nt_run_cycle (jack_driver_nt_t *drv) { return 0; }
static int dummy_nt_attach    (jack_driver_nt_t *drv) { return 0; }
static int dummy_nt_detach    (jack_driver_nt_t *drv) { return 0; }


/*
 * These are used in driver->nt_run for controlling whether or not
 * driver->engine->driver_exit() gets called (EXIT = call it, PAUSE = don't)
 */
#define DRIVER_NT_RUN   0
#define DRIVER_NT_EXIT  1
#define DRIVER_NT_PAUSE 2

static int
jack_driver_nt_attach (jack_driver_nt_t * driver, jack_engine_t * engine)
{
	driver->engine = engine;
	return driver->nt_attach (driver);
}

static int
jack_driver_nt_detach (jack_driver_nt_t * driver, jack_engine_t * engine)
{
	int ret;

	ret = driver->nt_detach (driver);
	driver->engine = NULL;

	return ret;
}

static int
jack_driver_nt_become_real_time (jack_driver_nt_t* driver)
{
        if (jack_acquire_real_time_scheduling (driver->nt_thread,
                                               driver->engine->rtpriority)) {
                return -1;
        }

#ifdef USE_MLOCK
        if (driver->engine->control->do_mlock
	    && (mlockall (MCL_CURRENT | MCL_FUTURE) != 0)) {
		jack_error ("cannot lock down memory for RT thread (%s)",
			    strerror (errno));
#ifdef ENSURE_MLOCK
		return -1;
#endif /* ENSURE_MLOCK */
        }
#endif /* USE_MLOCK */

        return 0;
}


static void *
jack_driver_nt_thread (void * arg)
{
	jack_driver_nt_t * driver = (jack_driver_nt_t *) arg;
	int rc = 0;
	int run;

	/* This thread may start running before pthread_create()
	 * actually stores the driver->nt_thread value.  It's safer to
	 * store it here as well. */
	driver->nt_thread = pthread_self();

	if (driver->engine->control->real_time)
		jack_driver_nt_become_real_time (driver);

	pthread_mutex_lock (&driver->nt_run_lock);

	while ( (run = driver->nt_run) == DRIVER_NT_RUN) {
		pthread_mutex_unlock (&driver->nt_run_lock);

		rc = driver->nt_run_cycle (driver);
		if (rc) {
			jack_error ("DRIVER NT: could not run driver cycle");
			goto out;
		}

		pthread_mutex_lock (&driver->nt_run_lock);
	}

	pthread_mutex_unlock (&driver->nt_run_lock);

 out:
	if (rc) {
		driver->engine->driver_exit (driver->engine);
	}
	pthread_exit (NULL);
}

static int
jack_driver_nt_start (jack_driver_nt_t * driver)
{
	int err;

	err = driver->nt_start (driver);
	if (err) {
		jack_error ("DRIVER NT: could not start driver");
		return err;
	}

	driver->nt_run = DRIVER_NT_RUN;

	err = pthread_create (&driver->nt_thread, NULL,
			      jack_driver_nt_thread, driver);
	if (err) {
		jack_error ("DRIVER NT: could not start driver thread!");
		driver->nt_stop (driver);
		return err;
	}

	return 0;
}

static int
jack_driver_nt_do_stop (jack_driver_nt_t * driver, int run)
{
	int err;

	pthread_mutex_lock (&driver->nt_run_lock);
	driver->nt_run = run;
	pthread_mutex_unlock (&driver->nt_run_lock);

	err = pthread_join (driver->nt_thread, NULL);
	if (err) {
		jack_error ("DRIVER NT: error waiting for driver thread: %s",
                            strerror (err));
		return err;
	}

	err = driver->nt_stop (driver);
	if (err) {
		jack_error ("DRIVER NT: error stopping driver");
		return err;
	}

	return 0;
}

static int
jack_driver_nt_stop (jack_driver_nt_t * driver)
{
	return jack_driver_nt_do_stop (driver, DRIVER_NT_EXIT);
}

static int
jack_driver_nt_bufsize (jack_driver_nt_t * driver, jack_nframes_t nframes)
{
	int err;
	int ret;

	err = jack_driver_nt_do_stop (driver, DRIVER_NT_PAUSE);
	if (err) {
		jack_error ("DRIVER NT: could not stop driver to change buffer size");
		driver->engine->driver_exit (driver->engine);
		return err;
	}

	ret = driver->nt_bufsize (driver, nframes);

	err = jack_driver_nt_start (driver);
	if (err) {
		jack_error ("DRIVER NT: could not restart driver during buffer size change");
		driver->engine->driver_exit (driver->engine);
		return err;
	}

	return ret;
}

void
jack_driver_nt_init (jack_driver_nt_t * driver)
{
	memset (driver, 0, sizeof (*driver));

	jack_driver_init ((jack_driver_t *) driver);

	driver->attach       = (JackDriverAttachFunction)    jack_driver_nt_attach;
	driver->detach       = (JackDriverDetachFunction)    jack_driver_nt_detach;
	driver->bufsize      = (JackDriverBufSizeFunction)   jack_driver_nt_bufsize;
	driver->stop         = (JackDriverStartFunction)     jack_driver_nt_stop;
	driver->start        = (JackDriverStopFunction)      jack_driver_nt_start;

	driver->nt_bufsize   = (JackDriverNTBufSizeFunction) dummy_bufsize;
	driver->nt_start     = (JackDriverNTStartFunction)   dummy_start;
	driver->nt_stop      = (JackDriverNTStopFunction)    dummy_stop;
	driver->nt_attach    =                               dummy_nt_attach;
	driver->nt_detach    =                               dummy_nt_detach;
	driver->nt_run_cycle =                               dummy_nt_run_cycle;


	pthread_mutex_init (&driver->nt_run_lock, NULL);
}

void
jack_driver_nt_finish     (jack_driver_nt_t * driver)
{
	pthread_mutex_destroy (&driver->nt_run_lock);
}
