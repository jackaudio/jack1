#ifndef _jack_sysdep_cycles_h_
#define _jack_sysdep_cycles_h_

#if defined(__i386__)
    
/* technically, i386 doesn't have a cycle counter, but
   running JACK on a real i386 seems like a ridiculuous
   target and gcc defines this for the entire x86 family
   including the [456]86 that do have the counter.
*/

#include <config/cpu/i386/cycles.h>

#elif defined(__x86_64)

#include <config/cpu/i486/cycles.h>

#elif defined(__powerpc__) || defined(__ppc__)   /* linux and OSX gcc use different tokens */

#include <config/cpu/powerpc/cycles.h>

#else

#include <config/cpu/generic/cycles.h>

#endif /* processor selection */

#endif /* _jack_sysdep_cycles_h_ */
