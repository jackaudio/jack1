/* Low-level functions for atomic operations.  Stub version.
   Copyright (C) 1997,2001 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#ifndef _ATOMICITY_H
#define _ATOMICITY_H	1

typedef int _Atomic_word;

static inline _Atomic_word 
__attribute__ ((__unused__))
__exchange_and_add(volatile _Atomic_word* mem, int val)
{
  return __sync_fetch_and_add(mem, val);
}

static inline void
__attribute__ ((__unused__))
__atomic_add(volatile _Atomic_word* mem, int val)
{
  __sync_add_and_fetch(mem, val);
}

#endif /* atomicity.h */
