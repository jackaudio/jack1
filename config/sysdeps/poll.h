#ifndef _jack_sysdep_poll_h_
#define _jack_sysdep_poll_h_

#if defined(__MACH__) && defined(__APPLE__)

#include <config/os/macosx/poll.h>

#else

#include <poll.h>

#endif

#endif /* _jack_sysdep_poll_h_ */
