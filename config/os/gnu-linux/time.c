/* -*- mode: c; c-file-style: "bsd"; -*- */
/*
    Copyright (C) 2001-2003 Paul Davis
    Copyright (C) 2005 Jussi Laako
    
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

#include <config.h>

static jack_time_t __jack_cpu_mhz = 0;
jack_time_t (*_jack_get_microseconds)(void) = 0;

#if defined(__gnu_linux__) && (defined(__i386__) || defined(__x86_64__))
#define HPET_SUPPORT
#define HPET_MMAP_SIZE	1024
#define HPET_PERIOD	0x004
#define HPET_COUNTER	0x0f0
#if defined(__x86_64__)
#define HPET_64BIT
#else
#define HPET_32BIT
#endif
static int hpet_fd;
static unsigned char *hpet_ptr;
static unsigned int hpet_period; /* period length in femto secs */
#endif /* defined(__gnu_linux__) && (__i386__ || __x86_64__) */

#ifdef HPET_SUPPORT
int
jack_hpet_init ()
{
	hpet_fd = open("/dev/hpet", O_RDONLY);
	if (hpet_fd < 0) {
		jack_error ("This system has no accessible HPET device (%s)", strerror (errno));
		return -1;
	}

	hpet_ptr = (unsigned char *) mmap(NULL, HPET_MMAP_SIZE,
					  PROT_READ, MAP_SHARED, hpet_fd, 0);
	if (hpet_ptr == NULL) {
		jack_error ("This system has no mappable HPET device (%s)", strerror (errno));
		close (hpet_fd);
		return -1;
	}

	/* this assumes period to be constant. if needed,
	   it can be moved to the clock access function 
	*/

	hpet_period = *((unsigned int *) (hpet_ptr + HPET_PERIOD));
	return 0;
}

static jack_time_t
jack_get_microseconds_from_hpet (void) 
{
#ifdef HPET_64BIT
	unsigned long long hpet_counter;
#else
	unsigned int hpet_counter;
#endif
	long double hpet_time;

#ifdef HPET_64BIT
	hpet_counter = *((unsigned long long *) (hpet_ptr + HPET_COUNTER));
#else
	hpet_counter = *((unsigned int *) (hpet_ptr + HPET_COUNTER));
#endif
	hpet_time = (long double) hpet_counter * (long double) hpet_period *
		(long double) 1e-9;
	return ((jack_time_t) (hpet_time + 0.5));
}

#else

static int
jack_hpet_init ()
{
	jack_error ("This version of JACK or this computer does not have HPET support.\n"
		    "Please choose a different clock source.");
	return -1;
}

static jack_time_t
jack_get_microseconds_from_hpet (void) 
{
	/* never called */
	return 0;
}

#endif /* HPET_SUPPORT */


jack_time_t 
jack_get_microseconds_from_cycles (void) {
	return get_cycles() / __jack_cpu_mhz;
}

/*
 * This is another kludge.  It looks CPU-dependent, but actually it
 * reflects the lack of standards for the Linux kernel formatting of
 * /proc/cpuinfo.
 */

jack_time_t
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

void
jack_init_time ()
{
	__jack_cpu_mhz = jack_get_mhz ();
}

void
jack_set_clock_source (jack_timer_type_t clocksrc)
{
	switch (clocksrc)
	{
	case JACK_TIMER_CYCLE_COUNTER:
		_jack_get_microseconds = jack_get_microseconds_from_cycles;
		break;

	case JACK_TIMER_HPET:
		if (jack_hpet_init () == 0) {
			_jack_get_microseconds = jack_get_microseconds_from_hpet;
		} else {
			_jack_get_microseconds = jack_get_microseconds_from_system;
		}
		break;

	case JACK_TIMER_SYSTEM_CLOCK:
	default:
		_jack_get_microseconds = jack_get_microseconds_from_system;
		break;
	}
}

