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

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <config.h>

#include <jack/shm.h>
#include <jack/internal.h>

typedef struct {
    shm_name_t name;
    char *address;
    int  shmid;				/* only needed for SysV shm */
} jack_shm_registry_entry_t;

static jack_shm_registry_entry_t *jack_shm_registry;
static int                        jack_shm_id_cnt;

void
jack_register_shm (char *shm_name, char *addr, int id)
{
	if (jack_shm_id_cnt < MAX_SHM_ID) {
		int entry = jack_shm_id_cnt++;
		strncpy (jack_shm_registry[entry].name, shm_name,
			 sizeof (shm_name_t));
		jack_shm_registry[entry].address = addr;
		jack_shm_registry[entry].shmid = id;
	}
}

static inline int
jack_lookup_shm (const char *shm_name)
{
	/***** NOT THREAD SAFE *****/

	int i;

	for (i = 0; i < jack_shm_id_cnt; ++i) {
		if (strcmp (jack_shm_registry[i].name, shm_name) == 0) {
			return i;
		}
	}
	return -1;			/* not found */
}

int
jack_initialize_shm ()
{
	void *addr;
	int id;

#ifdef USE_POSIX_SHM
	fprintf (stderr, "JACK compiled with POSIX SHM support\n");
#else
	fprintf (stderr, "JACK compiled with System V SHM support\n");
#endif

	if (jack_shm_registry != NULL) {
		return 0;
	}

	/* grab a chunk of memory to store shm ids in. this is 
	   to allow our parent to clean up all such ids when
	   if we exit. otherwise, they can get lost in crash
	   or debugger driven exits.
	*/
	if ((addr = jack_get_shm ("/jack-shm-registry",
				  (sizeof (jack_shm_registry_entry_t)
				   * MAX_SHM_ID), O_RDWR|O_CREAT, 0600,
				  PROT_READ|PROT_WRITE, &id))
	    == MAP_FAILED) {
		return -1;
	}

	jack_shm_registry = (jack_shm_registry_entry_t *) addr;
	jack_shm_id_cnt = 0;
	
	jack_register_shm ("/jack-shm-registry", addr, id);

	return 0;
}

void
jack_cleanup_shm ()
{
#if ! USE_POSIX_SHM
	char path[PATH_MAX+1];
	DIR *dir;
	struct dirent *dirent;
#endif
	int i;

	for (i = 0; i < jack_shm_id_cnt; i++) {
		jack_destroy_shm (jack_shm_registry[i].name);
	}
#if ! USE_POSIX_SHM

	snprintf (path, sizeof(path), "%s/jack/shm", jack_server_dir);
	if ((dir = opendir (path)) == NULL) {
		if (errno != ENOENT) {
		        jack_error ("cannot open jack shm directory (%s)",
				    strerror (errno));
		}
	} else {
		while ((dirent = readdir (dir)) != NULL) {
			char fullpath[PATH_MAX+1];
			snprintf (fullpath, sizeof (fullpath),
				  "%s/jack/shm/%s", jack_server_dir,
				  dirent->d_name);
			unlink (fullpath);
		}
	}
	closedir (dir);

	snprintf (path, sizeof(path), "%s/jack/shm", jack_server_dir);
	if (rmdir (path)) {
		if (errno != ENOENT) {
		        jack_error ("cannot remove JACK shm directory (%s)",
				    strerror (errno));
		}
	}
	snprintf (path, sizeof(path), "%s/jack", jack_server_dir);
	if (rmdir (path)) {
		if (errno != ENOENT) {
			jack_error ("cannot remove JACK directory (%s)",
				    strerror (errno));
		}
	}
#endif
}

#if USE_POSIX_SHM

void
jack_destroy_shm (const char *shm_name)
{
	shm_unlink (shm_name);
}

void
jack_release_shm (char *addr, size_t size)
{
	munmap (addr, size);
}

char *
jack_get_shm (const char *shm_name, size_t size, int perm, int mode, int prot,
	      int *not_really_used)
{
	int shm_fd;
	char *addr;

	if ((shm_fd = shm_open (shm_name, perm, mode)) < 0) {
		jack_error ("cannot create shm segment %s (%s)", shm_name,
			    strerror (errno));
		return MAP_FAILED;
	}

	if (perm & O_CREAT) {
		if (ftruncate (shm_fd, size) < 0) {
			jack_error ("cannot set size of engine shm registry "
				    "(%s)", strerror (errno));
			return MAP_FAILED;
		}
	}

	if ((addr = mmap (0, size, prot, MAP_SHARED, shm_fd, 0))
	    == MAP_FAILED) {
		jack_error ("cannot mmap shm segment %s (%s)", shm_name,
			    strerror (errno));
		shm_unlink (shm_name);
		close (shm_fd);
		return MAP_FAILED;
	}

	close (shm_fd);
	*not_really_used = 0;
	return addr;
}

char *
jack_resize_shm (const char *shm_name, size_t size, int perm, int mode,
		 int prot)
{
	int entry;
	int shm_fd;
	char *addr;
	struct stat statbuf;

	if ((entry = jack_lookup_shm (shm_name)) < 0) {
		jack_error ("attempt to resize unknown shm segment \"%s\"",
			    shm_name);
		return MAP_FAILED;
	}

	if ((shm_fd = shm_open (shm_name, perm, mode)) < 0) {
		jack_error ("cannot create shm segment %s (%s)", shm_name,
			    strerror (errno));
		return MAP_FAILED;
	}

	fstat (shm_fd, &statbuf);
	
	munmap (jack_shm_registry[entry].address, statbuf.st_size);

	if (perm & O_CREAT) {
		if (ftruncate (shm_fd, size) < 0) {
			jack_error ("cannot set size of engine shm registry "
				    "(%s)", strerror (errno));
			return MAP_FAILED;
		}
	}
		
	if ((addr = mmap (0, size, prot, MAP_SHARED, shm_fd, 0))
	    == MAP_FAILED) {
		jack_error ("cannot mmap shm segment %s (%s)", shm_name,
			    strerror (errno));
		shm_unlink (shm_name);
		close (shm_fd);
		return MAP_FAILED;
	}

	close (shm_fd);
	return addr;
}

#else /* USE_POSIX_SHM */

char
jack_hash_shm (jack_shmsize_t size)
{
	char log2size = 0;		/* log2 of size */

	if (size == 0)
		return log2size;	/* don't loop forever */

	/* remove low-order zeroes, counting them */
	while ((size & 1) == 0) {
		++log2size;
		size >>= 1;
	}

	return (char) ((size + log2size) & 0x7f);
}

void
jack_destroy_shm (const char *shm_name)
{
	int i = jack_lookup_shm (shm_name);

	if (i >= 0)
		shmctl (IPC_RMID, jack_shm_registry[i].shmid, NULL);
}

void
jack_release_shm (char *addr, size_t size)
{
	shmdt (addr);
}

char *
jack_get_shm (const char *shm_name, size_t size, int perm, int mode,
	      int prot, int* shmid)
{
	char *addr;
	key_t key;
	int shmflags;
	char path[PATH_MAX+1];
	struct stat statbuf;
	int status;

	/* note: no trailing '/' on basic path because we expect shm_name to
	   begin with one (as per POSIX shm API). */
	snprintf (path, sizeof(path), "%s/jack", jack_server_dir);
	if (mkdir (path, 0775)) {
		if (errno != EEXIST) {
			jack_error ("cannot create JACK directory (%s)",
				    strerror (errno));
			return MAP_FAILED;
		}
	}

	snprintf (path, sizeof(path), "%s/jack/shm", jack_server_dir);
	if (mkdir (path, 0775)) {
		if (errno != EEXIST) {
			jack_error ("cannot create JACK shm directory (%s)",
				    strerror (errno));
			return MAP_FAILED;
		}
	}
        
	snprintf (path, sizeof(path), "%s/jack/shm%s", jack_server_dir,
		  shm_name);
	if ((status = stat (path, &statbuf)) < 0) {
		int fd;

		if ((fd = open (path, O_RDWR|O_CREAT, 0775)) < 0) {
			jack_error ("cannot create shm file node for %s (%s)",
				    path, strerror (errno));
			return MAP_FAILED;
		}
		close (fd);
	}

	/* Hash the shm size to distinguish differently-sized segments
	 * with the same path name.  This allows jack_resize_shm() to
	 * allocate a new segment when the size changes with the same
	 * name but a different shmid. */
	if ((key = ftok (path, jack_hash_shm(size))) < 0) {
		jack_error ("cannot generate IPC key for shm segment %s (%s)",
			    path, strerror (errno));
		unlink (path);
		return MAP_FAILED;
	}
	
	/* XXX need to figure out how to do this without causing the
	   inode reallocation the next time this function is called
	   resulting in ftok() returning non-unique keys.
	*/

	/* unlink (path); */

	shmflags = mode;

	if (perm & O_CREAT) {
		shmflags |= IPC_CREAT;
	}

	if (perm & O_TRUNC) {
		shmflags |= IPC_EXCL;
	}

	if ((*shmid = shmget (key, size, shmflags)) < 0) {

		if (errno == EEXIST && (shmflags & IPC_EXCL)) {

			shmflags &= ~IPC_EXCL;

			if ((*shmid = shmget (key, size, shmflags)) < 0) {
				jack_error ("cannot get existing shm segment "
					    "for %s (%s)", shm_name,
					    strerror (errno));
				return MAP_FAILED;
			}

		} else {
			jack_error ("cannot create shm segment %s (%s)",
				    shm_name, strerror (errno));
			return MAP_FAILED;
		}
	}

	if ((addr = shmat (*shmid, 0, 0)) < 0) {
		jack_error ("cannot attach shm segment %s (%s)",
			    shm_name, strerror (errno));
		return MAP_FAILED;
	}

	return addr;
}

char *
jack_resize_shm (const char *shm_name, size_t size, int perm, int mode,
		 int prot)
{
	int entry = jack_lookup_shm (shm_name);

	if (entry < 0) {
		jack_error ("attempt to resize unknown shm segment \"%s\"",
			    shm_name);
		return MAP_FAILED;
	}

	/* There is no way to resize a System V shm segment.  So, we
	 * delete it and allocate a new one.  This is tricky, because
	 * the old segment will not disappear until all the clients
	 * have released it. */
	jack_destroy_shm (shm_name);
	jack_release_shm (jack_shm_registry[entry].address, size);
	jack_shm_registry[entry].address =
		jack_get_shm (shm_name, size, perm, mode, prot,
			      &jack_shm_registry[entry].shmid);

	return jack_shm_registry[entry].address;
}

#endif /* USE_POSIX_SHM */
