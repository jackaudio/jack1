/*
    Copyright (C) 2004 Jack O'Quin

    Generic version, overridden by OS-specific defines when available.
    In this case, that is necessary, because the generic version
    hasn't been written, yet.
    
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

#include <jack/internal.h>

#error No generic <sysdeps/time.h> available.

/* This is a kludge.  We need one global instantiation of this
 * variable in each address space.  So, libjack/client.c declares the
 * actual storage.  Other source files will see it as an extern. */
#define JACK_TIME_GLOBAL_DECL jack_time_t __jack_cpu_mhz
extern JACK_TIME_GLOBAL_DECL;

/* Need implementations. jack_init_time() should only be called ONCE
 * per process. */
static inline jack_time_t  jack_get_microseconds (void);
static inline void	   jack_init_time (void);

#endif /* __jack_time_h__ */
