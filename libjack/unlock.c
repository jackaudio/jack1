/* -*- mode: c; c-file-style: "bsd"; -*- */
/*
    Copyright (C) 2004 Paul Davis
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public License
    along with this program; if not, write to the Free Software 
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

*/

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <sys/mman.h>

#include "jack/unlock.h"
#include "jack/internal.h"

static char* blacklist[] = {
	"/libgtk",
	"/libqt",
	"/libfltk",
	"/wine/",
	NULL
};

static char* whitelist[] = {
	"/libc-",
	"/libardour",
	NULL
};

static char* library_roots[] = {
	"/lib",
	"/usr/lib",
	"/usr/local/lib",
	"/usr/X11R6/lib",
	"/opt/lib",       /* solaris-y */   
	"/opt/local/lib", /* common on OS X */
	NULL
};

void
cleanup_mlock ()
{
	FILE* map;
	size_t start;
	size_t end;
	char path[PATH_MAX+1];
	int unlock;
	int i;
	int whoknows;
	int looks_like_library;

	snprintf (path, sizeof(path), "/proc/%d/maps", getpid());

	if ((map = fopen (path, "r")) == NULL) {
		jack_error ("can't open map file");
		return;
	}

	while (!feof (map)) {

		unlock = 0;

		if (fscanf (map, "%zx-%zx %*s %*x %*d:%*d %d",
			    &start, &end, &whoknows) != 3) {
			break;
		}

		if (!whoknows) {
			continue;
		}

		fscanf (map, " %[^\n]", path);

		/* if it doesn't look like a library, forget it */

		looks_like_library = 0;

		for (i = 0; library_roots[i]; ++i) {
			if ((looks_like_library = (strstr (path, library_roots[i]) == path))) {
				break;
			}
		}

		if (!looks_like_library) {
			continue;
		}
		
		for (i = 0; blacklist[i]; ++i) {
			if (strstr (path, blacklist[i])) {
				unlock = 1;
				break;
			}
		}

		if (end - start > 1048576) {
			unlock = 1;
		}
		
		for (i = 0; whitelist[i]; ++i) {
			if (strstr (path, whitelist[i])) {
				unlock = 0;
				break;
			}
		}
		
		if (unlock) {
			jack_info ("unlocking %s", path);
			munlock ((char *) start, end - start);
		}
	}

	fclose (map);
}
	
	
