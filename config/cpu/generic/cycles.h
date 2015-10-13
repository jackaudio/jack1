/*
    Copyright (C) 2001 Paul Davis
    
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

*/

#ifndef __jack_cycles_h__
#define __jack_cycles_h__

/* generic solution that is not really a solution at all */

#include <sys/time.h>

typedef long cycles_t;

static inline cycles_t get_cycles(void)
{
       struct timespec time;
       clock_gettime(CLOCK_REALTIME, &time);

       return ((cycles_t) time.tv_sec * 1000000) + time.tv_nsec*1000;
}

#endif /* __jack_cycles_h__ */
