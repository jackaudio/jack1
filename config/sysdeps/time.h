#ifndef _jack_sysdep_time_h_
#define _jack_sysdep_time_h_

#if defined(__gnu_linux__)
#include <config/os/gnu-linux/time.h>
#elif defined(__MACH__) && defined(__APPLE__)
#include <config/os/macosx/time.h>
#else
#include <config/os/generic/time.h>
#endif

#endif /* _jack_sysdep_time_h_ */
