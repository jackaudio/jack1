/*
    Copyright (C) 2001-2003 Paul Davis

    This is the GNU/Linux version.
    
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

/*
 * This is another kludge.  It looks CPU-dependent, but actually it
 * reflects the lack of standards for the Linux kernel formatting of
 * /proc/cpuinfo.
 */
static inline jack_time_t
jack_get_mhz (void)
{
	FILE *f = fopen("/proc/cpuinfo", "r");
	if (f == 0)
	{
		perror("can't open /proc/cpuinfo\n");
		exit(1);
	}

	for ( ; ; )
	{
		jack_time_t mhz;
		int ret;
		char buf[1000];

		if (fgets(buf, sizeof(buf), f) == NULL) {
			jack_error ("FATAL: cannot locate cpu MHz in "
				    "/proc/cpuinfo\n");
			exit(1);
		}

#if defined(__powerpc__)
		ret = sscanf(buf, "clock\t: %" SCNu64 "MHz", &mhz);
#elif defined( __i386__ ) || defined (__hppa__)  || defined (__ia64__) || \
      defined(__x86_64__)
		ret = sscanf(buf, "cpu MHz         : %" SCNu64, &mhz);
#elif defined( __sparc__ )
		ret = sscanf(buf, "Cpu0Bogo        : %" SCNu64, &mhz);
#elif defined( __mc68000__ )
		ret = sscanf(buf, "Clocking:       %" SCNu64, &mhz);
#elif defined( __s390__  )
		ret = sscanf(buf, "bogomips per cpu: %" SCNu64, &mhz);
#else /* MIPS, ARM, alpha */
		ret = sscanf(buf, "BogoMIPS        : %" SCNu64, &mhz);
#endif 

		if (ret == 1)
		{
			fclose(f);
			return (jack_time_t)mhz;
		}
	}
}

/* This should only be called ONCE per process. */
static inline void 
jack_init_time ()
{
	__jack_cpu_mhz = jack_get_mhz ();
}

#endif /* __jack_time_h__ */
