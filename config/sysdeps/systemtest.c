#ifndef _jack_sysdep_systemtest_c_
#define _jack_sysdep_systemtest_c_

#if defined(__gnu_linux__)
#include <config/os/gnu-linux/systemtest.c>
#elif defined(__MACH__) && defined(__APPLE__)
/* relax */
#else
/* relax */
#endif

#endif /* _jack_sysdep_systemtest_c_ */
