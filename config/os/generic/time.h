/*
    Copyright (C) 2001-2004 Paul Davis, Tilman Linneweh

    Generic version, overridden by OS-specific definition when needed.
    
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
#ifndef __jack_time_h__
#define __jack_time_h__

#include <stdio.h>
#include <jack/internal.h>
#include <sysdeps/cycles.h>

/* This is a kludge.  We need one global instantiation of this
 * variable in each address space.  So, libjack/client.c declares the
 * actual storage.  Other source files will see it as an extern. */
#define JACK_TIME_GLOBAL_DECL jack_time_t __jack_cpu_mhz
extern JACK_TIME_GLOBAL_DECL;

static inline jack_time_t 
jack_get_microseconds (void) {
	return get_cycles() / __jack_cpu_mhz;
}

/* This function is inspired by similar code in MPLayer.
 * It should be quite portable
 */
static inline jack_time_t
jack_get_mhz (void)
{
       jack_time_t tsc_start, tsc_end;
       struct timeval tv_start, tv_end;
       long usec_delay;
       jack_time_t mhz;
                     
       tsc_start = get_cycles();   
       gettimeofday(&tv_start, NULL);
       usleep(100000);
       tsc_end = get_cycles();
       gettimeofday(&tv_end, NULL);
                
       usec_delay = 1000000 * (tv_end.tv_sec - tv_start.tv_sec)
           + (tv_end.tv_usec - tv_start.tv_usec);
       mhz = (tsc_end - tsc_start) / usec_delay;
       return mhz;
}

/* This should only be called ONCE per process. */
static inline void 
jack_init_time ()
{
	__jack_cpu_mhz = jack_get_mhz ();
}

#endif /* __jack_time_h__ */
