/*
    Copyright (C) 2004 Jack O'Quin

    MacOS X specific defines.

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

#ifndef _jack_os_defines
#define _jack_os_defines 1

#define JACK_CPP_VARARGS_BROKEN 1
#define JACK_USE_MACH_THREADS 1
#define JACK_DO_NOT_MLOCK 1
#define PORTAUDIO_H <PortAudio.h>
#define GETOPT_H <sysdeps/getopt.h>

/* MacOSX defines __POWERPC__ rather than __powerpc__ */
#if defined(__POWERPC__) && !defined(__powerpc__)
#define __powerpc__ __POWERPC__
#endif

#endif /* _jack_os_defines */
