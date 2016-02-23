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

/*
 * Interface with various machine-dependent headers derived from the
 * gcc/libstdc++.v3 sources.  We try to modify the GCC sources as
 * little as possible.  The following include is resolved using the
 * config/configure.hosts mechanism.  It will use an OS-dependent
 * version if available, otherwise the one for this CPU.  Some of
 * these files might not work with older GCC compilers.
 */
#include <sysdeps/atomicity.h>

/* These functions are defined for each platform.  The C++ library
 * function names start with "__" to avoid namespace pollution. */
#define exchange_and_add __exchange_and_add
#define atomic_add __atomic_add

#endif /* __jack_atomicity_h__ */
