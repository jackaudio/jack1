/*
    Copyright (C) 2005 Jussi Laako
     
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

#ifndef __jack_intsimd_h__
#define __jack_intsimd_h__

#ifdef USE_DYNSIMD
#if (defined(__i386__) || defined(__x86_64__))
#define ARCH_X86
#endif /* __i386__ || __x86_64__ */
#endif /* USE_DYNSIMD */

#ifdef ARCH_X86
#define ARCH_X86_SSE(x)		((x) & 0xff)
#define ARCH_X86_HAVE_SSE2(x)	(ARCH_X86_SSE(x) >= 2)
#define ARCH_X86_3DNOW(x)	((x) >> 8)
#define ARCH_X86_HAVE_3DNOW(x)	(ARCH_X86_3DNOW(x))

typedef float v2sf __attribute__((vector_size(8)));
typedef float v4sf __attribute__((vector_size(16)));
typedef v2sf * pv2sf;
typedef v4sf * pv4sf;

extern int cpu_type;

#endif /* ARCH_X86 */

#endif /* __jack_intsimd_h__ */

