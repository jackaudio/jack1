/* -*- mode: c; c-file-style: "bsd"; -*- */
/*
    Copyright (C) 2001-2003 Paul Davis
    Copyright (C) 2005 Jussi Laako
    
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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <jack/internal.h>
#include <jack/jack.h>
#include <jack/engine.h>

#include <sysdeps/time.h>
#include <sysdeps/cycles.h>

#include "local.h"

const char*
jack_clock_source_name (jack_timer_type_t src)
{
	switch (src) {
	case JACK_TIMER_CYCLE_COUNTER:
		return "cycle counter";
	case JACK_TIMER_HPET:
		return "hpet";
	case JACK_TIMER_SYSTEM_CLOCK:
#ifdef HAVE_CLOCK_GETTIME
		return "system clock via clock_gettime";
#else
		return "system clock via gettimeofday";
#endif
	}

	/* what is wrong with gcc ? */

	return "unknown";
}

#ifndef HAVE_CLOCK_GETTIME

jack_time_t 
jack_get_microseconds_from_system (void) {
	jack_time_t jackTime;
	struct timeval tv;

	gettimeofday (&tv, NULL);
	jackTime = (jack_time_t) tv.tv_sec * 1000000 + (jack_time_t) tv.tv_usec;
	return jackTime;
}

#else

jack_time_t 
jack_get_microseconds_from_system (void) {
	jack_time_t jackTime;
	struct timespec time;

	clock_gettime(CLOCK_MONOTONIC, &time);
	jackTime = (jack_time_t) time.tv_sec * 1e6 +
		(jack_time_t) time.tv_nsec / 1e3;
	return jackTime;
}

#endif /* HAVE_CLOCK_GETTIME */

/* everything below here should be system-dependent */

#include <sysdeps/time.c>


