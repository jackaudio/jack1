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

  Thread creation function including workarounds for real-time scheduling
  behaviour on different glibc versions.

  $Id$

*/

#include <config.h>

#include <jack/jack.h>
#include <jack/thread.h>
#include <jack/internal.h>

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "local.h"

#ifdef JACK_USE_MACH_THREADS
#include <sysdeps/pThreadUtilities.h>
#endif

static inline void
log_result (char *msg, int res)
{
	char outbuf[500];
	snprintf(outbuf, sizeof(outbuf),
		 "jack_create_thread: error %d %s: %s",
		 res, msg, strerror(res));
	jack_error(outbuf);
}

static void*
jack_thread_proxy (jack_thread_arg_t* arg)
{
	void* (*work)(void*);
	void* warg;
	jack_client_t* client = arg->client;
	int try_rt = 0;

	if (arg->realtime) {

#ifdef USE_CAPABILITIES

		if (client == 0) {

			/* we're creating a thread within jackd itself, don't
			   bother trying to acquire capabilities because either
			   jackd has them or it doesn't.
			*/

			try_rt = 1;

		} else {

			jack_request_t req;
			
			if (client->engine->has_capabilities != 0 &&
			    client->control->pid != 0 && 
			    client->engine->real_time != 0) {
				
				/* we need to ask the engine for realtime capabilities
				   before trying to run the thread work function
				*/
				
				req.type = SetClientCapabilities;
				req.x.cap_pid = getpid();
				
				jack_client_deliver_request (client, &req);
				
				if (req.status) {
					
					/* what to do? engine is running realtime, it
					   is using capabilities and has them
					   (otherwise we would not get an error
					   return) but for some reason it could not
					   give the client the required capabilities.
					   for now, allow the client to run, albeit
					   non-realtime.
					*/
					
					jack_error ("could not receive realtime capabilities, "
						    "client will run non-realtime");
				} else { 
					try_rt = 1;
				}
			}
		}
		
#else /* !USE_CAPABILITIES */

		try_rt = 1;

#endif /* USE_CAPABILITIES */
			
		if (try_rt) {
			jack_acquire_real_time_scheduling (pthread_self(), arg->priority);
		}
	}

	warg = arg->arg;
	work = arg->work_function;

	free (arg);
	
	return work (warg);
}

int
jack_create_thread (jack_client_t* client,
		    pthread_t* thread,
		    int priority,
		    int realtime,
		    void*(*start_routine)(void*),
		    void* arg)
{
#ifndef JACK_USE_MACH_THREADS
	pthread_attr_t attr;
	int policy;
	struct sched_param param;
	int actual_policy;
	struct sched_param actual_param;
	jack_thread_arg_t* thread_args;
#endif /* !JACK_USE_MACH_THREADS */

	int result = 0;

	if (!realtime) {
		result = pthread_create (thread, 0, start_routine, arg);
		if (result) {
			log_result("creating thread with default parameters",
				   result);
		}
		return result;
	}

	/* realtime thread. this disgusting mess is a reflection of
	 * the 2nd-class nature of RT programming under POSIX in
	 * general and Linux in particular.
	 */

#ifndef JACK_USE_MACH_THREADS

	pthread_attr_init(&attr);
	result = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	if (result) {
		log_result("requesting explicit scheduling", result);
		return result;
	}
	result = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	if (result) {
		log_result("requesting joinable thread creation", result);
		return result;
	}
	result = pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
	if (result) {
		log_result("requesting system scheduling scope", result);
		return result;
	}

	thread_args = (jack_thread_arg_t *) malloc (sizeof (jack_thread_arg_t));

	thread_args->client = client;
	thread_args->work_function = start_routine;
	thread_args->arg = arg;
	thread_args->realtime = 1;
	thread_args->priority = priority;

	result = pthread_create (thread, &attr, jack_thread_proxy, thread_args);
	if (result) {
		log_result ("creating realtime thread", result);
		return result;
	}

#else /* JACK_USE_MACH_THREADS */

	result = pthread_create (thread, 0, start_routine, arg);
	if (result) {
		log_result ("creating realtime thread", result);
		return result;
	}

	/* time constraint thread */
	setThreadToPriority (*thread, 96, TRUE, 10000000);
	
#endif /* JACK_USE_MACH_THREADS */

	return 0;
}

#if JACK_USE_MACH_THREADS 

int
jack_drop_real_time_scheduling (pthread_t thread)
{
	setThreadToPriority(thread, 31, FALSE, 10000000);
	return 0;       
}

int
jack_acquire_real_time_scheduling (pthread_t thread, int priority)
	//priority is unused
{
	setThreadToPriority(thread, 96, TRUE, 10000000);
	return 0;
}

#else /* !JACK_USE_MACH_THREADS */

int
jack_drop_real_time_scheduling (pthread_t thread)
{
	struct sched_param rtparam;
	int x;
	
	memset (&rtparam, 0, sizeof (rtparam));
	rtparam.sched_priority = 0;

	if ((x = pthread_setschedparam (thread, SCHED_OTHER, &rtparam)) != 0) {
		jack_error ("cannot switch to normal scheduling priority(%s)\n",
			    strerror (errno));
		return -1;
	}
        return 0;
}

int
jack_acquire_real_time_scheduling (pthread_t thread, int priority)
{
	struct sched_param rtparam;
	int x;
	
	memset (&rtparam, 0, sizeof (rtparam));
	rtparam.sched_priority = priority;
	
	if ((x = pthread_setschedparam (thread, SCHED_FIFO, &rtparam)) != 0) {
		jack_error ("cannot use real-time scheduling (FIFO at priority %d) "
			    "[for thread %d, from thread %d] (%d: %s)", 
			    rtparam.sched_priority, 
			    thread, pthread_self(),
			    x, strerror (x));
		return -1;
	}

        return 0;
}

#endif /* JACK_USE_MACH_THREADS */
