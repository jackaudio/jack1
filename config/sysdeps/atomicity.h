#ifndef _jack_sysdep_atomicity_h_
#define _jack_sysdep_atomicity_h_

#if defined(__i386__)

#include <config/cpu/i386/atomicity.h>

#elif defined(__x86_64)

/* x86_64 can use rdtsc just like i[456]86 */

#include <config/cpu/i386/atomicity.h>

#elif defined(__powerpc__) || defined(__ppc__) /* linux and OSX use different tokens */

#include <config/cpu/powerpc/atomicity.h>

#else

#include <config/cpu/generic/atomicity.h>

#endif /* processor selection */

#endif /* _jack_sysdep_atomicity_h_ */
