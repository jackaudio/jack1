/* This module provides a set of abstract shared memory interfaces
 * with support using both System V and POSIX shared memory
 * implementations.  The code is divided into three sections:
 *
 *	- common (interface-independent) code
 *	- POSIX implementation
 *	- System V implementation
 *
 * The implementation used is determined by whether USE_POSIX_SHM was
 * set in the ./configure step.
 */

/*
 * Copyright (C) 2003 Paul Davis
 * Copyright (C) 2004 Jack O'Quin
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id$
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
#include <sys/stat.h>
#include <sysdeps/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sysdeps/ipc.h>

#include <jack/shm.h>
#include <jack/internal.h>
#include <jack/version.h>

#ifdef USE_POSIX_SHM
static jack_shmtype_t jack_shmtype = shm_POSIX;
static char *shmtype_name = "POSIX";
#else
static jack_shmtype_t jack_shmtype = shm_SYSV;
static char *shmtype_name = "System V";
#endif

/* interface-dependent forward declarations */
static int	jack_access_registry (jack_shm_info_t *ri);
static void	jack_remove_shm (jack_shm_id_t *id);

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * common interface-independent section
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* global data */
jack_shm_id_t registry_id;		/* SHM id for the registry */
jack_shm_info_t registry_info = {	/* SHM info for the registry */
	.index = JACK_SHM_NULL_INDEX,
	.attached_at = MAP_FAILED
};

/* pointers to registry header and array */
static jack_shm_header_t* jack_shm_header = NULL;
static jack_shm_registry_t* jack_shm_registry = NULL;

/* jack_shm_lock_registry() serializes updates to the shared memory
 * segment JACK uses to keep track of the SHM segements allocated to
 * all its processes, including multiple servers.
 *
 * This is not a high-contention lock, but it does need to work across
 * multiple processes.  High transaction rates and realtime safety are
 * not required.  Any solution needs to at least be portable to POSIX
 * and POSIX-like systems.
 *
 * We must be particularly careful to ensure that the lock be released
 * if the owning process terminates abnormally.  Otherwise, a segfault
 * or kill -9 at the wrong moment could prevent JACK from ever running
 * again on that machine until after a reboot.
 */

#define JACK_SEMAPHORE_KEY 0x282929
#ifndef USE_POSIX_SHM
#define JACK_SHM_REGISTRY_KEY JACK_SEMAPHORE_KEY
#endif

static int semid = -1;

/* all semaphore errors are fatal -- issue message, but do not return */
static void
semaphore_error (char *msg)
{
	jack_error ("Fatal JACK semaphore error: %s (%s)",
		    msg, strerror (errno));
	abort ();
}

static void
semaphore_init ()
{
	key_t semkey = JACK_SEMAPHORE_KEY;
	struct sembuf sbuf;
	int create_flags = IPC_CREAT | IPC_EXCL
		| S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

	/* Get semaphore ID associated with this key. */
	if ((semid = semget(semkey, 0, 0)) == -1) {

		/* Semaphore does not exist - Create. */
		if ((semid = semget(semkey, 1, create_flags)) != -1) {

			/* Initialize the semaphore, allow one owner. */
			sbuf.sem_num = 0;
			sbuf.sem_op = 1;
			sbuf.sem_flg = 0;
			if (semop(semid, &sbuf, 1) == -1) {
				semaphore_error ("semop");
			}

		} else if (errno == EEXIST) {
			if ((semid = semget(semkey, 0, 0)) == -1) {
				semaphore_error ("semget");
			}

		} else {
			semaphore_error ("semget creation"); 
		}
	}
}

static inline void
semaphore_add (int value)
{
	struct sembuf sbuf;

	sbuf.sem_num = 0;
	sbuf.sem_op = value;
	sbuf.sem_flg = SEM_UNDO;
	if (semop(semid, &sbuf, 1) == -1) {
		semaphore_error ("semop");
	}
}

static void 
jack_shm_lock_registry (void)
{
	if (semid == -1)
		semaphore_init ();

	semaphore_add (-1);
}

static void 
jack_shm_unlock_registry (void)
{
	semaphore_add (1);
}

static void
jack_shm_init_registry ()
{
	/* registry must be locked */
	int i;

	memset (jack_shm_header, 0, JACK_SHM_REGISTRY_SIZE);

	jack_shm_header->magic = JACK_SHM_MAGIC;
	jack_shm_header->protocol = jack_protocol_version;
	jack_shm_header->type = jack_shmtype;
	jack_shm_header->size = JACK_SHM_REGISTRY_SIZE;
	jack_shm_header->hdr_len = sizeof (jack_shm_header_t);
	jack_shm_header->entry_len = sizeof (jack_shm_registry_t);

	for (i = 0; i < MAX_SHM_ID; ++i) {
		jack_shm_registry[i].index = i;
	}
}

static int
jack_shm_validate_registry ()
{
	/* registry must be locked */

	if ((jack_shm_header->magic == JACK_SHM_MAGIC)
	    && (jack_shm_header->protocol == jack_protocol_version)
	    && (jack_shm_header->type == jack_shmtype)
	    && (jack_shm_header->size == JACK_SHM_REGISTRY_SIZE)
	    && (jack_shm_header->hdr_len == sizeof (jack_shm_header_t))
	    && (jack_shm_header->entry_len == sizeof (jack_shm_registry_t))) {

		return 0;		/* registry OK */
	}

	jack_error ("incompatible shm registry (%s)", strerror (errno));

	/* Apparently, this registry was created by an older JACK
	 * version.  Delete it so we can try again. */
	jack_release_shm (&registry_info);
	jack_remove_shm (&registry_id);

	return -1;
}

/* gain addressibility to shared memory registration segment
 *
 * returns: 0 if successful
 */
int
jack_initialize_shm (void)
{
	int rc;

	if (jack_shm_header)
		return 0;		/* already initialized */

	jack_shm_lock_registry ();

	rc = jack_access_registry (&registry_info);

	switch (rc) {
	case 1:				/* newly-created registry */
		jack_shm_init_registry ();
		rc = 0;			/* success */
		break;
	case 0:				/* existing registry */
		if (jack_shm_validate_registry () == 0)
			break;
		/* else it was invalid, so fall through */
	case -2:			/* bad registry */
		/* it's gone now, so try again */
		rc = jack_access_registry (&registry_info);
		if (rc == 1) {		/* new registry created? */
			jack_shm_init_registry ();
			rc = 0;		/* problem solved */
		} else {

			jack_error ("incompatible shm registry (%s)",
				    strerror (errno));
#ifndef USE_POSIX_SHM
			jack_error ("to delete, use `ipcrm -M 0x%0.8x'",
				    JACK_SHM_REGISTRY_KEY);
#endif
			rc = -1;	/* FUBAR */
		}
		break;
	default:			/* failure return code */
	}

	jack_shm_unlock_registry ();
	return rc;
}

void
jack_destroy_shm (jack_shm_info_t* si)
{
	/* must NOT have the registry locked */
	if (si->index == JACK_SHM_NULL_INDEX)
		return;			/* segment not allocated */

	jack_remove_shm (&jack_shm_registry[si->index].id);
	jack_release_shm_info (si->index);
}

jack_shm_registry_t *
jack_get_free_shm_info ()
{
	/* registry must be locked */
	jack_shm_registry_t* si = NULL;
	int i;

	for (i = 0; i < MAX_SHM_ID; ++i) {
		if (jack_shm_registry[i].size == 0) {
			break;
		}
	}
	
	if (i < MAX_SHM_ID) {
		si = &jack_shm_registry[i];
	}

	return si;
}

void
jack_release_shm_info (jack_shm_registry_index_t index)
{
	/* must NOT have the registry locked */
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

	fprintf (stderr, "JACK compiled with %s SHM support.\n", shmtype_name);

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

			int index = copy.index;

			if ((index >= 0)  && (index < MAX_SHM_ID)) {
				jack_remove_shm (&jack_shm_registry[index].id);
				jack_shm_registry[index].size = 0;
				jack_shm_registry[index].allocator = 0;
			}
			r->size = 0;
			r->allocator = 0;
		}
	}

	jack_shm_unlock_registry ();

	return TRUE;
}

#ifdef USE_POSIX_SHM

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * POSIX interface-dependent functions
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* gain addressibility to SHM registry segment
 *
 * sets up global registry pointers, if successful
 *
 * returns: 1 if newly created
 *          0 if existing registry
 *         -1 if unsuccessful
 *         -2 if registry existed, but was the wrong size
 */
static int
jack_access_registry (jack_shm_info_t *ri)
{
	/* registry must be locked */
	int shm_fd;
	int new_registry = 0;
	int rc = -1;
	int perm;
	jack_shmsize_t size = JACK_SHM_REGISTRY_SIZE;

	/* grab a chunk of memory to store shm ids in. this is 
	   to allow clean up of all segments whenever JACK
	   starts (or stops). */
	perm = O_RDWR;
	strncpy (registry_id, "/jack-shm-registry", sizeof (registry_id));

	/* try without O_CREAT to see if it already exists */
	if ((shm_fd = shm_open (registry_id, perm, 0666)) < 0) {

		if (errno == ENOENT) {
			
			/* it doesn't exist, so create it */
			perm = O_RDWR|O_CREAT;
			if ((shm_fd =
			     shm_open (registry_id, perm, 0666)) < 0) {
				jack_error ("cannot create shm registry segment"
					    " (%s)", strerror (errno));
				goto error;
			}
			new_registry = 1;

		} else {

			jack_error ("cannot open existing shm registry segment"
				    " (%s)", strerror (errno));
			goto error;
		}
	}

	if (perm & O_CREAT) {
		if (ftruncate (shm_fd, size) < 0) {
			jack_error ("cannot set size of engine shm registry 1"
			"(%s)", strerror (errno));
			jack_remove_shm (&registry_id);
			rc = -2;
			goto error;
		}
	}
  
	if ((ri->attached_at = mmap (0, size, PROT_READ|PROT_WRITE,
				     MAP_SHARED, shm_fd, 0)) == MAP_FAILED) {
		jack_error ("cannot mmap shm registry segment (%s)",
			    strerror (errno));
		goto error;
	}

	/* set up global pointers */
	ri->index = JACK_SHM_REGISTRY_INDEX;
	jack_shm_header = ri->attached_at;
	jack_shm_registry = (jack_shm_registry_t *) (jack_shm_header + 1);

	rc = new_registry;

 error:
	close (shm_fd);
	return rc;
}

static void
jack_remove_shm (jack_shm_id_t *id)
{
	shm_unlink (*id);
}

void
jack_release_shm (jack_shm_info_t* si)
{
	//printf("client->jack_release_shm \n");
	if (si->attached_at != MAP_FAILED) {
		//printf("client->jack_release_shm 1 \n");
		munmap (si->attached_at, jack_shm_registry[si->index].size);
	}
}

int
jack_shmalloc (const char *shm_name, jack_shmsize_t size, jack_shm_info_t* si)
{
	jack_shm_registry_t* registry;
	int shm_fd;
	int rc = -1;

	jack_shm_lock_registry ();

	if ((registry = jack_get_free_shm_info ())) {

		if ((shm_fd = shm_open (shm_name, O_RDWR|O_CREAT, 0666)) >= 0) {

			if (ftruncate (shm_fd, size) >= 0) {

				close (shm_fd);
				registry->size = size;
				snprintf (registry->id, sizeof (registry->id),
					  "%s", shm_name);
				registry->allocator = getpid();
				si->index = registry->index;
				si->attached_at = MAP_FAILED; /* not attached */
				rc = 0;	/* success */

			} else {
				jack_error ("cannot set size of engine shm "
					    "registry 0 (%s)",
					    strerror (errno));
			}

		} else {
			jack_error ("cannot create shm segment %s (%s)",
				    shm_name, strerror (errno));
		}
	}

	jack_shm_unlock_registry ();
	return rc;
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
				     MAP_SHARED, shm_fd, 0)) == MAP_FAILED) {
		jack_error ("cannot mmap shm segment %s (%s)", registry->id,
			    strerror (errno));
		close (shm_fd);
		return -1;
	}

	close (shm_fd);
	return 0;
}

#else

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * System V interface-dependent functions
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* gain addressibility to SHM registry segment
 *
 * sets up global registry pointers, if successful
 *
 * returns: 1 if newly created
 *          0 if existing registry
 *         -1 if unsuccessful
 *         -2 if registry existed, but was the wrong size
 */
static int
jack_access_registry (jack_shm_info_t *ri)
{
	/* registry must be locked */
	int shmflags = 0666;
	int shmid;
	int new_registry = 0;		/* true if new segment created */
	key_t key = JACK_SHM_REGISTRY_KEY;
	jack_shmsize_t size = JACK_SHM_REGISTRY_SIZE;

	/* grab a chunk of memory to store shm ids in. this is 
	   to allow our parent to clean up all such ids when
	   if we exit. otherwise, they can get lost in crash
	   or debugger driven exits.
	*/

	/* try without IPC_CREAT to check if it already exists */
	if ((shmid = shmget (key, size, shmflags)) < 0) {
		switch (errno) {
		case ENOENT:		/* did not exist */
			if ((shmid = shmget (key, size,
					     shmflags|IPC_CREAT)) < 0) {
				jack_error ("cannot create shm registry segment"
					    " (%s)", strerror (errno));
				return -1;
			}
			new_registry = 1;
			break;

		case EINVAL:		/* exists, but too small */

			/* try to remove it */
			if ((shmid = shmget (key, 1, shmflags)) >= 0) {
				shmctl (shmid, IPC_RMID, NULL);
				return -2;
			}

		default:
			jack_error ("unable to access shm registry (%s)",
				    strerror (errno));
			return -1;
		}
	}

	if ((ri->attached_at = shmat (shmid, 0, 0)) < 0) {
		jack_error ("cannot attach shm registry segment (%s)",
			    strerror (errno));
		return -1;
	}

	/* set up global pointers */
	ri->index = JACK_SHM_REGISTRY_INDEX;
	jack_shm_header = ri->attached_at;
	jack_shm_registry = (jack_shm_registry_t *) (jack_shm_header + 1);
	registry_id = shmid;

	return new_registry;
}

static void
jack_remove_shm (jack_shm_id_t *id)
{
	shmctl (*id, IPC_RMID, NULL);
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
	int rc = -1;
	jack_shm_registry_t* registry;

	jack_shm_lock_registry ();

	if ((registry = jack_get_free_shm_info ())) {

		shmflags = 0666 | IPC_CREAT | IPC_EXCL;

		if ((shmid = shmget (IPC_PRIVATE, size, shmflags)) >= 0) {

			registry->size = size;
			registry->id = shmid;
			registry->allocator = getpid();
			si->index = registry->index;
			si->attached_at = MAP_FAILED; /* not attached */
			rc = 0;

		} else {
			jack_error ("cannot create shm segment %s (%s)",
				    name_not_used, strerror (errno));
		}
	}

	jack_shm_unlock_registry ();

	return rc;
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
