/*
    Copyright (C) 2003 Paul Davis
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    $Id$
*/
#include <config.h>

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/shm.h>

#include <jack/shm.h>
#include <jack/internal.h>

static jack_shm_registry_t* jack_shm_registry;

static void 
jack_shm_lock_registry ()
{
	/* XXX magic with semaphores here */
}

static void 
jack_shm_unlock_registry ()
{
	/* XXX magic with semaphores here */
}

jack_shm_registry_t *
jack_get_free_shm_info ()
{
	jack_shm_registry_t* si = NULL;
	int i;

	jack_shm_lock_registry ();

	for (i = 0; i < MAX_SHM_ID; ++i) {
		if (jack_shm_registry[i].size == 0) {
			break;
		}
	}
	
	if (i < MAX_SHM_ID) {
		si = &jack_shm_registry[i];
	}
	
	jack_shm_unlock_registry ();

	return si;
}

void
jack_release_shm_info (jack_shm_registry_index_t index)
{
	if (jack_shm_registry[index].allocator == getpid()) {
		jack_shm_lock_registry ();
		jack_shm_registry[index].size = 0;
		jack_shm_registry[index].allocator = 0;
		jack_shm_unlock_registry ();
	}
}

void
jack_cleanup_shm (void)
{
	int i;
	int destroy;
	jack_shm_info_t copy;

	jack_initialize_shm ();
	jack_shm_lock_registry ();

	for (i = 0; i < MAX_SHM_ID; i++) {
		jack_shm_registry_t* r;

		r = &jack_shm_registry[i];
		copy.index = r->index;
		destroy = FALSE;

		if (r->allocator == getpid()) {
			
			/* allocated by this process, so unattach 
			   and destroy.
			*/
			
			jack_release_shm (&copy);
			destroy = TRUE;
			
		} else {
			
			if (kill (r->allocator, 0)) {
				if (errno == ESRCH) {
					
					/* allocator no longer exists, so destroy */
					
					destroy = TRUE;
				}
			}
		}
		
		if (destroy) {

			jack_destroy_shm (&copy);
			
			r->size = 0;
			r->allocator = 0;
		}
	}

	jack_shm_unlock_registry ();
}

#ifdef USE_POSIX_SHM

int
jack_initialize_shm (void)
{
	int shm_fd;
	jack_shmsize_t size;
	int new_registry = FALSE;
	int ret = -1;
	int perm;

	if (jack_shm_registry != NULL) {
		return 0;
	}

	/* grab a chunk of memory to store shm ids in. this is 
	   to allow clean up of all segments whenever JACK
	   starts (or stops).
	*/

	size = sizeof (jack_shm_registry_t) * MAX_SHM_ID;

	jack_shm_lock_registry ();
	
	perm = O_RDWR;

	/* try without O_CREAT to see if it already exists */
	
	if ((shm_fd = shm_open ("/jack-shm-registry", perm, 0666)) < 0) {

		if (errno == ENOENT) {
				
			perm = O_RDWR|O_CREAT;
			
			/* it doesn't exist, so create it */

			if ((shm_fd = shm_open ("/jack-shm-registry", perm, 0666)) < 0) {
				jack_error ("cannot create shm registry segment (%s)",
					    strerror (errno));
				goto out;
			}
			new_registry = TRUE;

		} else {

			jack_error ("cannot open existing shm registry segment (%s)",
				    strerror (errno));
			goto out;
		}
	}

	if (perm & O_CREAT) {
		if (ftruncate (shm_fd, size) < 0) {
			jack_error ("cannot set size of engine shm registry 1"
			"(%s)", strerror (errno));
			goto out;
		}
	}
  
	if ((jack_shm_registry = mmap (0, size, PROT_READ|PROT_WRITE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED) {
		jack_error ("cannot mmap shm registry segment (%s)",
			    strerror (errno));
		goto out;
	}

	if (new_registry) {
		int i;

		memset (jack_shm_registry, 0, size);
		for (i = 0; i < MAX_SHM_ID; ++i) {
			jack_shm_registry[i].index = i;
		}
		fprintf (stderr, "JACK compiled with POSIX SHM support\n");
	}

	ret = 0;

  out:
	close (shm_fd);
	jack_shm_unlock_registry ();
	return ret;
}

void
jack_destroy_shm (jack_shm_info_t* si)
{
	shm_unlink (jack_shm_registry[si->index].id);
	jack_release_shm_info (si->index);
}

void
jack_release_shm (jack_shm_info_t* si)
{
	if (si->attached_at != MAP_FAILED) {
		munmap (si->attached_at, jack_shm_registry[si->index].size);
	}
}

int
jack_shmalloc (const char *shm_name, jack_shmsize_t size, jack_shm_info_t* si)
{
	jack_shm_registry_t* registry;
	int shm_fd;
	int perm = O_RDWR|O_CREAT;

	if ((registry = jack_get_free_shm_info ()) == NULL) {
		return -1;
	}

	if ((shm_fd = shm_open (shm_name, O_RDWR|O_CREAT, 0666)) < 0) {
		jack_error ("cannot create shm segment %s (%s)", shm_name,
			    strerror (errno));
		return -1;
	}
        
	if (perm & O_CREAT) {
		if (ftruncate (shm_fd, size) < 0) {
				jack_error ("cannot set size of engine shm registry 0"
							"(%s)", strerror (errno));
				return -1;
		}
	}

	close (shm_fd);

	registry->size = size;
	snprintf (registry->id, sizeof (registry->id), "%s", shm_name);
	registry->allocator = getpid();
	
	si->index = registry->index;
	si->attached_at = MAP_FAILED;	/* segment not attached */
	
	return 0;
}

int
jack_attach_shm (jack_shm_info_t* si)
{
	int shm_fd;
	jack_shm_registry_t *registry = &jack_shm_registry[si->index];

	if ((shm_fd = shm_open (registry->id,
				O_RDWR, 0666)) < 0) {
		jack_error ("cannot open shm segment %s (%s)", registry->id,
			    strerror (errno));
		return -1;
	}

	if ((si->attached_at = mmap (0, registry->size, PROT_READ|PROT_WRITE,
				     MAP_SHARED, shm_fd, 0)) == MAP_FAILED) {
		jack_error ("cannot mmap shm segment %s (%s)", 
			    registry->id,
			    strerror (errno));
		close (shm_fd);
		return -1;
	}

	close (shm_fd);

	return 0;
}

int
jack_resize_shm (jack_shm_info_t* si, jack_shmsize_t size)
{
	int shm_fd;
	jack_shm_registry_t *registry = &jack_shm_registry[si->index];

	if ((shm_fd = shm_open (registry->id, O_RDWR, 0666)) < 0) {
		jack_error ("cannot create shm segment %s (%s)", registry->id,
			    strerror (errno));
		return -1;
	}

	munmap (si->attached_at, registry->size);

	if (ftruncate (shm_fd, size) < 0) {
		jack_error ("cannot set size of shm segment %s "
			    "(%s)", registry->id, strerror (errno));
		return -1;
	}
		
	if ((si->attached_at = mmap (0, size, PROT_READ|PROT_WRITE,
				     MAP_SHARED, shm_fd, 0))
	    == MAP_FAILED) {
		jack_error ("cannot mmap shm segment %s (%s)", registry->id,
			    strerror (errno));
		close (shm_fd);
		return -1;
	}

	close (shm_fd);
	return 0;
}

#else /* USE_POSIX_SHM */

#define JACK_SHM_REGISTRY_KEY 0x282929

int
jack_initialize_shm (void)
{
	int shmflags;
	int shmid;
	key_t key;
	jack_shmsize_t size;
	int new_registry = FALSE;
	int ret = -1;

	if (jack_shm_registry != NULL) {
		return 0;
	}

	/* grab a chunk of memory to store shm ids in. this is 
	   to allow our parent to clean up all such ids when
	   if we exit. otherwise, they can get lost in crash
	   or debugger driven exits.
	*/

	shmflags = 0666;
	key = JACK_SHM_REGISTRY_KEY;
	size = sizeof (jack_shm_registry_t) * MAX_SHM_ID;

	jack_shm_lock_registry ();

	/* try without IPC_CREAT to check if it already exists */

	if ((shmid = shmget (key, size, shmflags)) < 0) {
		if (errno == ENOENT) {
			if ((shmid = shmget (key, size, shmflags|IPC_CREAT)) < 0) {
				jack_error ("cannot create shm registry segment (%s)",
					    strerror (errno));
				goto out;
			}
			
			new_registry = TRUE;
			
		} else {

			jack_error ("cannot use existing shm registry segment (%s)",
				    strerror (errno));
			goto out;
		}
	}

	if ((jack_shm_registry = shmat (shmid, 0, 0)) < 0) {
		jack_error ("cannot attach shm registry segment (%s)",
			    strerror (errno));
		goto out;
	}

	if (new_registry) {
		int i;
		memset (jack_shm_registry, 0, size);
		for (i = 0; i < MAX_SHM_ID; ++i) {
			jack_shm_registry[i].index = i;
		}
		fprintf (stderr, "JACK compiled with System V SHM support\n");
	}

	ret = 0;

  out:
	jack_shm_unlock_registry ();
	return ret;
}

void
jack_destroy_shm (jack_shm_info_t* si)
{
	shmctl (jack_shm_registry[si->index].id, IPC_RMID, NULL);
	jack_release_shm_info (si->index);
}

void
jack_release_shm (jack_shm_info_t* si)
{
	if (si->attached_at != MAP_FAILED) {
		shmdt (si->attached_at);
	}
}

int
jack_shmalloc (const char* name_not_used, jack_shmsize_t size, jack_shm_info_t* si) 
{
	int shmflags;
	int shmid;
	jack_shm_registry_t* registry;

	if ((registry = jack_get_free_shm_info ()) == NULL) {
		return -1;
	}

	shmflags = 0666 | IPC_CREAT | IPC_EXCL;

	if ((shmid = shmget (IPC_PRIVATE, size, shmflags)) < 0) {
		jack_error ("cannot create shm segment %s (%s)",
			    name_not_used, strerror (errno));
		return -1;
	}

	registry->size = size;
	registry->id = shmid;
	registry->allocator = getpid();

	si->index = registry->index;
	si->attached_at = MAP_FAILED;	/* segment not attached */

	return 0;
}

int
jack_attach_shm (jack_shm_info_t* si)
{
	if ((si->attached_at = shmat (jack_shm_registry[si->index].id, 0, 0)) < 0) {
		jack_error ("cannot attach shm segment (%s)",
			    strerror (errno));
		jack_release_shm_info (si->index);
		return -1;
	}
	return 0;
}

int
jack_resize_shm (jack_shm_info_t* si, jack_shmsize_t size)
{
	/* There is no way to resize a System V shm segment.  So, we
	 * delete it and allocate a new one.  This is tricky, because
	 * the old segment will not disappear until all the clients
	 * have released it. We can only do what we can from here.
	 */

	jack_release_shm (si);
	jack_destroy_shm (si);

	if (jack_shmalloc ("not used", size, si)) {
		return -1;
	}

	return jack_attach_shm (si);
}

#endif /* !USE_POSIX_SHM */
