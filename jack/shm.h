#ifndef __jack_shm_h__
#define __jack_shm_h__

#include <jack/types.h>

#define MAX_SHM_ID 256 /* likely use is more like 16 */

extern int  jack_initialize_shm ();
extern void jack_cleanup_shm ();

extern void jack_register_shm (char *shm_name, char *address, int id);

extern char *jack_get_shm (const char *shm_name, size_t size, int perm, int mode, int prot, int *id);
extern void  jack_release_shm (char* addr, size_t size);
extern void  jack_destroy_shm (const char *shm_name);

extern char *jack_resize_shm (const char *shm_name, size_t size, int perm, int mode, int prot);

#endif /* __jack_shm_h__ */
