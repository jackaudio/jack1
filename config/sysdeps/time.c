#ifndef _jack_sysdep_time_c_
#define _jack_sysdep_time_c_

#if defined(__gnu_linux__)
#include <config/os/gnu-linux/time.c>
#elif defined(__MACH__) && defined(__APPLE__)
#include <config/os/macosx/time.c>
#else
#include <config/os/generic/time.c>
#endif

#endif /* _jack_sysdep_time_c_ */
