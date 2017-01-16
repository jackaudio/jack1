/*
    Copyright (C) 2004 Jack O'Quin

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

#ifndef __jack_atomicity_h__
#define __jack_atomicity_h__

#if __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)

#include <stdatomic.h>

typedef atomic_int _Atomic_word;

static inline int exchange_and_add(volatile _Atomic_word* obj, int value)
{
    return atomic_fetch_add_explicit(obj, value, memory_order_relaxed);
}

#else

typedef int _Atomic_word;

static inline int exchange_and_add(volatile _Atomic_word* obj, int value)
{
    return __atomic_fetch_add(obj, value, __ATOMIC_RELAXED);
}

#endif

#endif /* __jack_atomicity_h__ */
