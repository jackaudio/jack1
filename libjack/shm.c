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
#include <jack/version.h>

static jack_shm_header_t* jack_shm_header = NULL;
static jack_shm_registry_t* jack_shm_registry = NULL;

static void 
jack_shm_lock_registry (void)
{
	/* XXX magic with semaphores here */
	//JOQ: this is really needed now with multiple servers
}

static void 
jack_shm_unlock_registry (void)
{
	/* XXX magic with semaphores here */
	//JOQ: this is really needed now with multiple servers
}

static int
jack_shm_create_registry (jack_shmtype_t type)
{
	/* registry must be locked */
	int i;

	memset (jack_shm_header, 0, JACK_SHM_REGISTRY_SIZE);

	jack_shm_header->magic = JACK_SHM_MAGIC;
	jack_shm_header->protocol = jack_protocol_version;
	jack_shm_header->type = type;
	jack_shm_header->size = JACK_SHM_REGISTRY_SIZE;
	jack_shm_header->hdr_len = sizeof (jack_shm_header_t);
	jack_shm_header->entry_len = sizeof (jack_shm_registry_t);

	for (i = 0; i < MAX_SHM_ID; ++i) {
		jack_shm_registry[i].index = i;
	}

	return 0;			/* success */
}

static int
jack_shm_validate_registry (jack_shmtype_t type)
{
	/* registry must be locked */

	if ((jack_shm_header->magic == JACK_SHM_MAGIC)
	    && (jack_shm_header->protocol == jack_protocol_version)
	    && (jack_shm_header->type == type)
	    && (jack_shm_header->size == JACK_SHM_REGISTRY_SIZE)
	    && (jack_shm_header->hdr_len == sizeof (jack_shm_header_t))
	    && (jack_shm_header->entry_len == sizeof (jack_shm_registry_t))) {

		return 0;		/* registry OK */
	}

	jack_error ("incompatible JACK shm registry");
	return -1;
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

/* Claim server_name for this process.  
 *
 * returns 0 if successful
 *	   EEXIST if server_name was already active for this user
 *	   ENOSPC if server registration limit reached
 */
int
jack_register_server (const char *server_name)
{
	int i;
	pid_t my_pid = getpid ();

	jack_shm_lock_registry ();

	/* See if server_name already registered.  Since server names
	 * are per-user, we register the server directory path name,
	 * which must be unique. */
	for (i = 0; i < MAX_SERVERS; i++) {

		if (strncmp (jack_shm_header->server[i].name,
			     jack_server_dir (server_name),
			     JACK_SERVER_NAME_SIZE) != 0)
			continue;	/* no match */

		if (jack_shm_header->server[i].pid == my_pid)
			return 0;	/* it's me */

		/* see if server still exists */
		if (kill (jack_shm_header->server[i].pid, 0) == 0) {
			return EEXIST;	/* other server running */
		}
	}

	/* find a free entry */
	for (i = 0; i < MAX_SERVERS; i++) {
		if (jack_shm_header->server[i].pid == 0)
			break;
	}

	if (i >= MAX_SERVERS)
		return ENOSPC;		/* out of space */

	/* claim it */
	jack_shm_header->server[i].pid = my_pid;
	strncpy (jack_shm_header->server[i].name,
		 jack_server_dir (server_name),
		 JACK_SERVER_NAME_SIZE);

	jack_shm_unlock_registry ();

	return 0;
}

/* release server_name registration */
void
jack_unregister_server (const char *server_name /* unused */)
{
	int i;
	pid_t my_pid = getpid ();

	jack_shm_lock_registry ();

	for (i = 0; i < MAX_SERVERS; i++) {
		if (jack_shm_header->server[i].pid == my_pid) {
			jack_shm_header->server[i].pid = 0;
			memset (jack_shm_header->server[i].name, 0,
				JACK_SERVER_NAME_SIZE);
		}
	}

	jack_shm_unlock_registry ();
}

/* called for server startup and termination */
int
jack_cleanup_shm ()
{
	int i;
	int destroy;
	jack_shm_info_t copy;
	pid_t my_pid = getpid ();

	jack_shm_lock_registry ();
		
	for (i = 0; i < MAX_SHM_ID; i++) {
		jack_shm_registry_t* r;

		r = &jack_shm_registry[i];
		copy.index = r->index;
		destroy = FALSE;

		/* ignore unused entries */
		if (r->allocator == 0)
			continue;

		/* is this my shm segment? */
		if (r->allocator == my_pid) {

			/* allocated by this process, so unattach 
			   and destroy. */
			jack_release_shm (&copy);
			destroy = TRUE;

		} else {

			/* see if allocator still exists */
			if (kill (r->allocator, 0)) {
				if (errno == ESRCH) {
					/* allocator no longer exists,
					 * so destroy */
					destroy = TRUE;
				}
			}
		}
		
		if (destroy) {

			if (copy.index >= 0  && copy.index < MAX_SHM_ID) {
				jack_destroy_shm (&copy);
			}
			r->size = 0;
			r->allocator = 0;
		}
	}

	jack_shm_unlock_registry ();

	return TRUE;
}

#ifdef USE_POSIX_SHM

/* gain addressibility to shared memory registration segment */
int
jack_initialize_shm (void)
{
	int shm_fd;
	int new_registry = FALSE;
	int ret = -1;
	int perm;
	jack_shmsize_t size = JACK_SHM_REGISTRY_SIZE;

	if (jack_shm_header) {
		return 0;
	}

	/* grab a chunk of memory to store shm ids in. this is 
	   to allow clean up of all segments whenever JACK
	   starts (or stops). */
	jack_shm_lock_registry ();
	perm = O_RDWR;

	/* try without O_CREAT to see if it already exists */
	if ((shm_fd = shm_open ("/jack-shm-registry", perm, 0666)) < 0) {

		if (errno == ENOENT) {
			
			/* it doesn't exist, so create it */
			perm = O_RDWR|O_CREAT;
			if ((shm_fd =
			     shm_open ("/jack-shm-registry", perm, 0666)) < 0) {
				jack_error ("cannot create shm registry segment"
					    " (%s)", strerror (errno));
				goto out;
			}
			new_registry = TRUE;

		} else {

			jack_error ("cannot open existing shm registry segment"
				    " (%s)", strerror (errno));
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
  
	if ((jack_shm_header = mmap (0, size, PROT_READ|PROT_WRITE,
				       MAP_SHARED, shm_fd, 0)) == MAP_FAILED) {
		jack_error ("cannot mmap shm registry segment (%s)",
			    strerror (errno));
		goto out;
	}

	jack_shm_registry = (jack_shm_registry_t *) (jack_shm_header + 1);

	if (new_registry) {

		ret = jack_shm_create_registry (shm_POSIX);
		fprintf (stderr, "JACK compiled with POSIX SHM support\n");

	} else {
		ret = jack_shm_validate_registry (shm_POSIX);
	}

  out:
	close (shm_fd);
	jack_shm_unlock_registry ();
	return ret;
}

void
jack_destroy_shm (jack_shm_info_t* si)
{
	if (si->index == -1)
		return;			/* segment not allocated */

	shm_unlink (jack_shm_registry[si->index].id);
	jack_release_shm_info (si->index);
}

void
jack_release_shm (jack_shm_info_t* si)
{
	printf("client->jack_release_shm \n");
	
	if (si->attached_at != MAP_FAILED) {
		printf("client->jack_release_shm 1 \n");
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
				jack_error ("cannot set size of engine shm "
					    "registry 0 (%s)",
					    strerror (errno));
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

/*
int
jack_resize_shm (jack_shm_info_t* si, jack_shmsize_t size)
{
	int shm_fd;
	//steph
	int res;
	jack_shm_registry_t *registry = &jack_shm_registry[si->index];

	if ((shm_fd = shm_open (registry->id, O_RDWR, 0666)) < 0) {
		jack_error ("cannot create shm segment %s (%s)", registry->id,
			    strerror (errno));
		return -1;
	}

	res = munmap (si->attached_at, registry->size);
	printf("munmap %ld\n", res);
	
		
	if ((res = ftruncate (shm_fd, size)) < 0) {
		jack_error ("cannot set size of shm segment %s "
			    "(%s)", registry->id, strerror (errno));
		printf("ftruncate %ld\n", res);
		return -1;
	}
	
		
	if ((si->attached_at = mmap (0, size, PROT_READ|PROT_WRITE,
				     MAP_SHARED, shm_fd, 0)) == MAP_FAILED) {
		jack_error ("cannot mmap shm segment %s (%s)", registry->id,
			    strerror (errno));
		close (shm_fd);
		return -1;
	}

	close (shm_fd);
	return 0;
}
*/

#else /* USE_POSIX_SHM */

/* gain addressibility to shared memory registration segment */
int
jack_initialize_shm (void)
{
	int shmflags;
	int shmid;
	key_t key;
	int new_registry = FALSE;
	int ret = -1;
	jack_shmsize_t size = JACK_SHM_REGISTRY_SIZE;

	if (jack_shm_header) {
		return 0;
	}

	/* grab a chunk of memory to store shm ids in. this is 
	   to allow our parent to clean up all such ids when
	   if we exit. otherwise, they can get lost in crash
	   or debugger driven exits.
	*/

	shmflags = 0666;
	key = JACK_SHM_REGISTRY_KEY;

	jack_shm_lock_registry ();

	/* try without IPC_CREAT to check if it already exists */

	if ((shmid = shmget (key, size, shmflags)) < 0) {
		if (errno == ENOENT) {
			if ((shmid = shmget (key, size,
					     shmflags|IPC_CREAT)) < 0) {
				jack_error ("cannot create shm registry segment"
					    " (%s)", strerror (errno));
				goto out;
			}
			new_registry = TRUE;
			
		} else {		/* probably different size */

			jack_error ("incompatible shm registry (%s)\n"
				    "to delete, use `ipcrm -M 0x%0.8x'",
				    strerror (errno), key);
			goto out;
		}
	}

	if ((jack_shm_header = shmat (shmid, 0, 0)) < 0) {
		jack_error ("cannot attach shm registry segment (%s)",
			    strerror (errno));
		goto out;
	}

	jack_shm_registry = (jack_shm_registry_t *) (jack_shm_header + 1);

	if (new_registry) {

		ret = jack_shm_create_registry (shm_SYSV);
		fprintf (stderr, "JACK compiled with System V SHM support\n");

	} else {
		ret = jack_shm_validate_registry (shm_SYSV);
	}

  out:
	jack_shm_unlock_registry ();
	return ret;
}

void
jack_destroy_shm (jack_shm_info_t* si)
{
	if (si->index == -1)
		return;			/* segment not allocated */

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
jack_shmalloc (const char* name_not_used, jack_shmsize_t size,
	       jack_shm_info_t* si) 
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
	if ((si->attached_at = shmat (jack_shm_registry[si->index].id,
				      0, 0)) < 0) {
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
