/*
 *  Copyright (C) 2004 Rui Nuno Capela, Steve Harris
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2.1 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *  
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  $Id$
 */

#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>

#include <jack/messagebuffer.h>

#define MB_BUFFERS		32		/* must be 2^n */

static char mb_buffers[MB_BUFFERS][MB_BUFFERSIZE];
static const char *mb_prefix;
static unsigned int mb_initialized = 0;
static unsigned int mb_inbuffer = 0;
static unsigned int mb_outbuffer = 0;
static pthread_t mb_writer_thread;

static void *
mb_thread_func(void *arg)
{
	while (mb_initialized) {
		usleep(1000);
		while (mb_outbuffer != mb_inbuffer) {
			fprintf(stderr, "%s%s", mb_prefix,
				mb_buffers[mb_outbuffer]);
			mb_outbuffer = (mb_outbuffer + 1) & (MB_BUFFERS - 1);
		}
	}

	return NULL;
}

void 
jack_messagebuffer_init (const char *prefix)
{
	if (mb_initialized)
		return;

	mb_initialized = 1;

	mb_prefix = prefix;
	if (pthread_create(&mb_writer_thread, NULL, &mb_thread_func, NULL) != 0)
		mb_initialized = 0;
}

void 
jack_messagebuffer_exit ()
{
	if (!mb_initialized)
		return;

	mb_initialized = 0;

	pthread_join(&mb_writer_thread, NULL);
}


void 
jack_messagebuffer_add (const char *msg)
{
	strncpy(mb_buffers[mb_inbuffer], msg, MB_BUFFERSIZE - 1);
	mb_inbuffer = (mb_inbuffer + 1) & (MB_BUFFERS - 1);
}
