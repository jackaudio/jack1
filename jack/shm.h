#ifndef __jack_shm_h__
#define __jack_shm_h__

#include <limits.h>
#include <sys/types.h>
#include <jack/types.h>

#define MAX_SHM_ID 256 /* likely use is more like 16 per jackd */

#ifdef USE_POSIX_SHM
typedef shm_name_t jack_shm_id_t;
#else
typedef int jack_shm_id_t;
#endif /* !USE_POSIX_SHM */

typedef int16_t jack_shm_registry_index_t;

/** 
 * A structure holding information about shared memory
 * allocated by JACK. this version persists across
 * invocations of JACK, and can be used by multiple
 * JACK servers. It contains no pointers and is valid
 * across address spaces.
 */
typedef struct _jack_shm_registry {
    pid_t                     allocator; /* PID that created shm segment */
    jack_shmsize_t            size;      /* needed for POSIX unattach */
    jack_shm_registry_index_t index;     /* offset into the registry */
    jack_shm_id_t             id;        /* API specific, see above */
} jack_shm_registry_t;

/** 
 * a structure holding information about shared memory
 * allocated by JACK. this version is valid only
 * for a given address space. It contains a pointer
 * indicating where the shared memory has been
 * attached to the address space.
 */
typedef struct _jack_shm_info {
    jack_shm_registry_index_t index;       /* offset into the registry */
    char*                     attached_at; /* address where attached */
} jack_shm_info_t;

/* utility functions used only within JACK */

extern void jack_shm_copy_from_registry (jack_shm_info_t*, 
					 jack_shm_registry_index_t);
extern void jack_shm_copy_to_registry (jack_shm_info_t*,
				       jack_shm_registry_index_t*);
extern void jack_release_shm_info (jack_shm_registry_index_t);

static inline char* jack_shm_addr (jack_shm_info_t* si) {
	return si->attached_at;
}

/* here beginneth the API */

extern int  jack_initialize_shm (void);
extern void jack_cleanup_shm (void);

extern int  jack_shmalloc (const char *shm_name, jack_shmsize_t size, jack_shm_info_t* result);
extern void jack_release_shm (jack_shm_info_t*);
extern void jack_destroy_shm (jack_shm_info_t*);
extern int  jack_attach_shm (jack_shm_info_t*);
extern int  jack_resize_shm (jack_shm_info_t*, jack_shmsize_t size);

#endif /* __jack_shm_h__ */
