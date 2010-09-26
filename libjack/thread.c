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

jack_thread_creator_t jack_thread_creator = pthread_create;

void
jack_set_thread_creator (jack_thread_creator_t jtc) 
{
	jack_thread_creator = jtc;
}

static inline void
log_result (char *msg, int res)
{
	char outbuf[500];
	snprintf(outbuf, sizeof(outbuf),
		 "jack_client_create_thread: error %d %s: %s",
		 res, msg, strerror(res));
	jack_error(outbuf);
}

static void
maybe_get_capabilities (jack_client_t* client)
{
#ifdef USE_CAPABILITIES

	if (client != 0) {
		
		jack_request_t req;

		if (client->engine->has_capabilities != 0) {
			
			/* we need to ask the engine for realtime capabilities
			   before trying to run the thread work function
			*/
			
                        VALGRIND_MEMSET (&req, 0, sizeof (req));
		
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
				
			}
		}
	}
#endif /* USE_CAPABILITIES */
}	

static void*
jack_thread_proxy (void* varg)
{
	jack_thread_arg_t* arg = (jack_thread_arg_t*) varg;
	void* (*work)(void*);
	void* warg;
	jack_client_t* client = arg->client;

	char buf[JACK_THREAD_STACK_TOUCH];
	int i;

	for (i = 0; i < JACK_THREAD_STACK_TOUCH; i++) {
                buf[i] = (char) (i & 0xff);
	}

	if (arg->realtime) {
		maybe_get_capabilities (client);
		jack_acquire_real_time_scheduling (pthread_self(), arg->priority);
	}

	warg = arg->arg;
	work = arg->work_function;

	free (arg);
	
	return work (warg);
}

int
jack_client_create_thread (jack_client_t* client,
			   pthread_t* thread,
			   int priority,
			   int realtime,
			   void*(*start_routine)(void*),
			   void* arg)
{
#ifndef JACK_USE_MACH_THREADS
	pthread_attr_t attr;
	jack_thread_arg_t* thread_args;
#endif /* !JACK_USE_MACH_THREADS */

	int result = 0;

	if (!realtime) {
		result = jack_thread_creator (thread, 0, start_routine, arg);
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
#ifdef __OpenBSD__
	result = pthread_attr_setscope(&attr, PTHREAD_SCOPE_PROCESS);
#else
	result = pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
#endif
	if (result) {
		log_result("requesting system scheduling scope", result);
		return result;
	}

        result = pthread_attr_setstacksize(&attr, THREAD_STACK); 
        if (result) {
                log_result("setting thread stack size", result);
                return result;
        }

	if ((thread_args = (jack_thread_arg_t *) malloc (sizeof (jack_thread_arg_t))) == NULL) {
		return -1;
	}

	thread_args->client = client;
	thread_args->work_function = start_routine;
	thread_args->arg = arg;
	thread_args->realtime = 1;
	thread_args->priority = priority;

	result = jack_thread_creator (thread, &attr, jack_thread_proxy, thread_args);
	if (result) {
		log_result ("creating realtime thread", result);
		return result;
	}

#else /* JACK_USE_MACH_THREADS */

	result = jack_thread_creator (thread, 0, start_routine, arg);
	if (result) {
		log_result ("creating realtime thread", result);
		return result;
	}

	/* time constraint thread */
	setThreadToPriority (*thread, 96, TRUE, 10000000);
	
#endif /* JACK_USE_MACH_THREADS */

	return 0;
}

int
jack_client_real_time_priority (jack_client_t* client)
{
	if (!client->engine->real_time) {
		return -1;
	}
	
	return client->engine->client_priority;
}

int
jack_client_max_real_time_priority (jack_client_t* client)
{
	if (!client->engine->real_time) {
		return -1;
	}

	return client->engine->max_client_priority;
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

