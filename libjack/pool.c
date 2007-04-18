/*
    Copyright (C) 2001-2003 Paul Davis
    
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

#ifdef HAVE_POSIX_MEMALIGN
#define _XOPEN_SOURCE 600
#endif
#include <stdlib.h>
#include <config.h>
#include <jack/pool.h>

/* XXX need RT-pool based allocator here */
void *
jack_pool_alloc (size_t bytes)
{
#ifdef HAVE_POSIX_MEMALIGN
	void* m;
	int	err = posix_memalign (&m, 64, bytes);
	return (!err) ? m : 0;
#else
	return malloc (bytes);
#endif /* HAVE_POSIX_MEMALIGN */
}

void
jack_pool_release (void *ptr)
{
	free (ptr);
}
