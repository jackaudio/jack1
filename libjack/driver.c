/*
    Copyright (C) 2001 Paul Davis
    
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

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include <config.h>

#include <jack/driver.h>
#include <jack/internal.h>
#include <jack/error.h>

static int dummy_attach (jack_driver_t *drv, jack_engine_t *eng) { return 0; }
static int dummy_detach (jack_driver_t *drv, jack_engine_t *eng) { return 0; }
static jack_nframes_t dummy_wait (jack_driver_t *drv, int fd, int *status, float *delayed_usecs) { *status = 0; *delayed_usecs = 0; return 0; }
static int dummy_process (jack_driver_t *drv, jack_nframes_t nframes) { return 0; }
static int dummy_stop (jack_driver_t *drv) { return 0; }
static int dummy_start (jack_driver_t *drv) { return 0; }

void
jack_driver_init (jack_driver_t *driver)
{
	memset (driver, 0, sizeof (*driver));

	driver->attach = dummy_attach;
	driver->detach = dummy_detach;
	driver->wait = dummy_wait;
	driver->process = dummy_process;
	driver->start = dummy_start;
	driver->stop = dummy_stop;
}

