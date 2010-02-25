/*
 * messagebuffer.c -- realtime-safe message handling for jackd.
 *
 *  This interface is included in libjack so backend drivers can use
 *  it, *not* for external client processes.  It implements the
 *  VERBOSE() and MESSAGE() macros in a realtime-safe manner.
 */

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
 */

#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>

#include <jack/messagebuffer.h>
#include <jack/atomicity.h>
#include <jack/internal.h>

/* MB_NEXT() relies on the fact that MB_BUFFERS is a power of two */
#define MB_BUFFERS	128
#define MB_NEXT(index) ((index+1) & (MB_BUFFERS-1))
#define MB_BUFFERSIZE	256		/* message length limit */

static char mb_buffers[MB_BUFFERS][MB_BUFFERSIZE];
static volatile unsigned int mb_initialized = 0;
static volatile unsigned int mb_inbuffer = 0;
static volatile unsigned int mb_outbuffer = 0;
static volatile _Atomic_word mb_overruns = 0;
static pthread_t mb_writer_thread;
static pthread_mutex_t mb_write_lock;
static pthread_cond_t mb_ready_cond;
static void (*mb_thread_init_callback)(void*) = 0;
static void* mb_thread_init_callback_arg = 0;

static void
mb_flush()
{
	/* called WITHOUT the mb_write_lock */
	while (mb_outbuffer != mb_inbuffer) {
		jack_info(mb_buffers[mb_outbuffer]);
		mb_outbuffer = MB_NEXT(mb_outbuffer);
	}
}

static void *
mb_thread_func(void *arg)
{
	/* The mutex is only to eliminate collisions between multiple
	 * writer threads and protect the condition variable. */
	pthread_mutex_lock(&mb_write_lock);

	while (mb_initialized) {
		pthread_cond_wait(&mb_ready_cond, &mb_write_lock);

		if (mb_thread_init_callback) {
			/* the client asked for all threads to run a thread
			   initialization callback, which includes us.
			*/
			mb_thread_init_callback (mb_thread_init_callback_arg);
			mb_thread_init_callback = 0;

			/* note that we've done it */
			pthread_cond_signal(&mb_ready_cond);
		}

		/* releasing the mutex reduces contention */
		pthread_mutex_unlock(&mb_write_lock);
		mb_flush();
		pthread_mutex_lock(&mb_write_lock);
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

	mb_overruns = 0;
	mb_initialized = 1;

	if (jack_thread_creator (&mb_writer_thread, NULL, &mb_thread_func, NULL) != 0)
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

	if (mb_overruns)
		jack_error("WARNING: %d message buffer overruns!",
			mb_overruns);

	pthread_mutex_destroy(&mb_write_lock);
	pthread_cond_destroy(&mb_ready_cond);
}


void 
jack_messagebuffer_add (const char *fmt, ...)
{
	char msg[MB_BUFFERSIZE];
	va_list ap;

	/* format the message first, to reduce lock contention */
	va_start(ap, fmt);
	vsnprintf(msg, MB_BUFFERSIZE, fmt, ap);
	va_end(ap);

	if (!mb_initialized) {
		/* Unable to print message with realtime safety.
		 * Complain and print it anyway. */
		fprintf(stderr, "ERROR: messagebuffer not initialized: %s",
			msg);
		return;
	}

	if (pthread_mutex_trylock(&mb_write_lock) == 0) {
		strncpy(mb_buffers[mb_inbuffer], msg, MB_BUFFERSIZE);
		mb_inbuffer = MB_NEXT(mb_inbuffer);
		pthread_cond_signal(&mb_ready_cond);
		pthread_mutex_unlock(&mb_write_lock);
	} else {			/* lock collision */
		atomic_add(&mb_overruns, 1);
	}
}

void
jack_messagebuffer_thread_init (void (*cb)(void*), void* arg)
{
	pthread_mutex_lock (&mb_write_lock);

	/* set up the callback */
	mb_thread_init_callback_arg = arg;
	mb_thread_init_callback = cb;

	/* wake msg buffer thread */
	pthread_cond_signal(&mb_ready_cond);

	/* wait for it to be done */
	pthread_cond_wait(&mb_ready_cond, &mb_write_lock);

	/* and we're done */
	pthread_mutex_unlock (&mb_write_lock);
}	
