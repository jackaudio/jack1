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

#define MB_BUFFERS	64		/* must be 2^n */

static char mb_buffers[MB_BUFFERS][MB_BUFFERSIZE];
static unsigned int mb_initialized = 0;
static unsigned int mb_inbuffer = 0;
static unsigned int mb_outbuffer = 0;
static pthread_t mb_writer_thread;
static pthread_mutex_t mb_write_lock;
static pthread_cond_t mb_ready_cond;

static void
mb_flush()
{
	while (mb_outbuffer != mb_inbuffer) {
		fputs(mb_buffers[mb_outbuffer], stderr);
		mb_outbuffer = (mb_outbuffer + 1) & (MB_BUFFERS - 1);
	}
}

static void *
mb_thread_func(void *arg)
{
	pthread_mutex_lock(&mb_write_lock);

	while (mb_initialized) {
		pthread_cond_wait(&mb_ready_cond, &mb_write_lock);
		mb_flush();
	}

	pthread_mutex_unlock(&mb_write_lock);

	return NULL;
}

void 
jack_messagebuffer_init ()
{
	if (mb_initialized)
		return;

	pthread_mutex_init(&mb_write_lock, NULL);
	pthread_cond_init(&mb_ready_cond, NULL);

	mb_initialized = 1;

	if (pthread_create(&mb_writer_thread, NULL, &mb_thread_func, NULL) != 0)
		mb_initialized = 0;
}

void 
jack_messagebuffer_exit ()
{
	if (!mb_initialized)
		return;

	pthread_mutex_lock(&mb_write_lock);
	mb_initialized = 0;
	pthread_cond_signal(&mb_ready_cond);
	pthread_mutex_unlock(&mb_write_lock);

	pthread_join(mb_writer_thread, NULL);
	mb_flush();

	pthread_mutex_destroy(&mb_write_lock);
	pthread_cond_destroy(&mb_ready_cond);
}


void 
jack_messagebuffer_add (const char *msg)
{
	if (pthread_mutex_trylock(&mb_write_lock) == 0) {
		strncpy(mb_buffers[mb_inbuffer], msg, MB_BUFFERSIZE - 1);
		mb_inbuffer = (mb_inbuffer + 1) & (MB_BUFFERS - 1);
		pthread_cond_signal(&mb_ready_cond);
		pthread_mutex_unlock(&mb_write_lock);
	}
}
