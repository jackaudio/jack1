/*
    Copyright (C) 2004 Paul Davis

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

#ifndef __jack_thread_h__
#define __jack_thread_h__

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>

/** @file thread.h
 *
 * A library functions to standardize thread creation for both jackd and its
 * clients.
 */

/**
 *
 */
int jack_create_thread (pthread_t* thread,
			int priority,
			int realtime, /* boolean */
			void*(*start_routine)(void*),
			void* arg);

/**
 *
 */
extern int jack_acquire_real_time_scheduling (pthread_t, int priority);

/**
 *
 */
extern int jack_drop_real_time_scheduling (pthread_t);

#ifdef __cplusplus
}
#endif

#endif /* __jack_thread_h__ */
