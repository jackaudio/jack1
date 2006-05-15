#ifndef _jack_sysdep_ipc_h_
#define _jack_sysdep_ipc_h_

#if defined(__MACH__) && defined(__APPLE__)
#include <config/os/macosx/ipc.h>
#else
#include <config/os/generic/ipc.h>
#endif

#endif /* _jack_sysdep_ipc_h_ */
