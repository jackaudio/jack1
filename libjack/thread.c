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

#include <jack/thread.h>
#include <jack/internal.h>

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifdef JACK_USE_MACH_THREADS
#include <sysdeps/pThreadUtilities.h>
#endif

#define log_internal(msg, res, fatal)	                             \
  if (res) {                                                         \
    char outbuf[500];                                                \
    snprintf(outbuf, sizeof(outbuf),                                 \
             "jack_create_thread: error %d %s: %s",                  \
             res, msg, strerror(res));                               \
    jack_error(outbuf);                                              \
    if (fatal)                                                       \
      return res;                                                    \
  }

#define log_result(msg) log_internal(msg, result, 1);
#define log_result_nonfatal(msg) log_internal(msg, result, 0);

int
jack_create_thread (pthread_t* thread,
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
#endif /* JACK_USE_MACH_THREADS */
	int result = 0;

	if (!realtime) {
		result = pthread_create (thread, 0, start_routine, arg);
		log_result("creating thread with default parameters");

		return 0;
	}

	/* realtime thread. this disgusting mess is a
	 * reflection of the 2nd-class nature of RT
	 * programming under POSIX in general and Linux in particular.
	 */

#ifndef JACK_USE_MACH_THREADS

	pthread_attr_init(&attr);
	policy = SCHED_FIFO;
	param.sched_priority = priority;
	result = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	log_result("requesting explicit scheduling");
	result = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	log_result("requesting joinable thread creation");
	result = pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
	log_result("requesting system scheduling scope");
	result = pthread_attr_setschedpolicy(&attr, policy);
	log_result("requesting non-standard scheduling policy");
	result = pthread_attr_setschedparam(&attr, &param);
	log_result("requesting thread priority");
	
	/* with respect to getting scheduling class+priority set up
	   correctly, there are three possibilities here: 

	   a) the call sets them and returns zero
	      ===================================

	      this is great, obviously.

	   b) the call fails to set them and returns an error code
	      ====================================================

  	      this could happen for a number of reasons,
	      but the important one is that we do not have the
	      priviledges required to create a realtime
	      thread. this could be correct, or it could be
	      bogus: there is at least one version of glibc
	      that checks only for UID in
	      pthread_attr_setschedpolicy(), and does not
	      check capabilities.
	      
	   c) the call fails to set them and does not return an error code
              ============================================================

	      this last case is caused by a stupid workaround in NPTL 0.60
	      where scheduling parameters are simply ignored, with no indication
	      of an error.
	*/

	result = pthread_create (thread, &attr, start_routine, arg);

	if (result) {

		/* this workaround temporarily switches the
		   current thread to the proper scheduler
		   and priority, using a call that
		   correctly checks for capabilities, then
		   starts the realtime thread so that it
		   can inherit them and finally switches
		   the current thread back to what it was
		   before.
		*/
		
		int current_policy;
		struct sched_param current_param;
		pthread_attr_t inherit_attr;
		
		current_policy = sched_getscheduler (0);
		sched_getparam (0, &current_param);
		
		result = sched_setscheduler (0, policy, &param);
		log_result("switching current thread to rt for inheritance");
		
		pthread_attr_init (&inherit_attr);
		result = pthread_attr_setscope (&inherit_attr,
						PTHREAD_SCOPE_SYSTEM);
		log_result("requesting system scheduling scope for inheritance");
		result = pthread_attr_setinheritsched (&inherit_attr,
						       PTHREAD_INHERIT_SCHED);
		log_result("requesting inheritance of scheduling parameters");
		result = pthread_create (thread, &inherit_attr, start_routine,
					 arg);
		log_result_nonfatal("creating real-time thread by inheritance");
		
		sched_setscheduler (0, current_policy, &current_param);
		
		if (result)
			return result;
	}
	
	/* Still here? Good. Let's see if this worked... */

	result = pthread_getschedparam (*thread, &actual_policy, &actual_param);
	log_result ("verifying scheduler parameters");

	if (actual_policy == policy &&
	    actual_param.sched_priority == param.sched_priority) {

		/* everything worked OK */

		return 0;
	}

	/* we failed to set the sched class and priority,
	 * even though no error was returned by
	 * pthread_create(). fix this by setting them
	 * explicitly, which as far as is known will
	 * work even when using thread attributes does not.
	 */

	result = pthread_setschedparam (*thread, policy, &param);
	log_result("setting scheduler parameters after thread creation");

#else /* JACK_USE_MACH_THREADS */

	result = pthread_create (thread, 0, start_routine, arg);
	log_result ("creating realtime thread");
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
		jack_error ("cannot use real-time scheduling (FIFO/%d) "
			    "(%d: %s)", rtparam.sched_priority, x,
			    strerror (x));
		return -1;
	}
        return 0;
}

#endif /* JACK_USE_MACH_THREADS */
