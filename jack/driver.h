/*
    Copyright (C) 2001 Paul Davis 

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

#ifndef __jack_driver_h__
#define __jack_driver_h__

#include <pthread.h>
#include <jack/types.h>
#include <jack/port.h>

typedef float         gain_t;
typedef long          channel_t;

typedef	enum  {
	Lock = 0x1,
	NoLock = 0x2,
	Sync = 0x4,
	NoSync = 0x8
} ClockSyncStatus;

typedef void (*ClockSyncListenerFunction)(channel_t,ClockSyncStatus,void*);

typedef struct {
    unsigned long id;
    ClockSyncListenerFunction function;
    void *arg;
} ClockSyncListener;

struct _jack_engine;
struct _jack_driver;

typedef int       (*JackDriverAttachFunction)(struct _jack_driver *, struct _jack_engine *);
typedef int       (*JackDriverDetachFunction)(struct _jack_driver *, struct _jack_engine *);
typedef int       (*JackDriverReadFunction)(struct _jack_driver *, jack_nframes_t nframes);
typedef int       (*JackDriverWriteFunction)(struct _jack_driver *, jack_nframes_t nframes);
typedef int       (*JackDriverNullCycleFunction)(struct _jack_driver *, jack_nframes_t nframes);
typedef int       (*JackDriverStopFunction)(struct _jack_driver *);
typedef int       (*JackDriverStartFunction)(struct _jack_driver *);
typedef jack_nframes_t (*JackDriverWaitFunction)(struct _jack_driver *, int fd, int *status, float *delayed_usecs);

/* 
   Call sequence summary:

     1) engine loads driver via runtime dynamic linking
	 - calls jack_driver_load
	 - we lookup "driver_initialize" and execute it
     2) engine attaches to driver
     3) engine starts driver
     4) while (1) {
           engine->wait ();
	   engine->audio_cycle ();
        }
     5) engine stops driver
     6) engine detaches from driver
     7) engine calls driver `finish' routine, if any

     note that stop/start may be called multiple times in the event of an
     error return from the `wait' function.
*/

#ifdef _ANNOTATED_DRIVER_DECLARATION_

#define JACK_DRIVER_DECL

/* the driver should set this to be the interval it expects to elapse
   between returning from the `wait' function. if set to zero, it
   implies that the driver does not expect regular periodic wakeups.
 */
    jack_time_t period_usecs;

/* the driver should set this within its "wait" function to indicate
   the UST of the most recent determination that the engine cycle
   should run. it should not be set if the "extra_fd" argument of
   the wait function is set to a non-zero value.
 */
    jack_time_t last_wait_ust;

/* this is not used by the driver. it should not be written to or
   modified in any way
 */

    void *handle;

/* this should perform any cleanup associated with the driver. it will
   be called when jack server process decides to get rid of the
   driver. in some systems, it may not be called at all, so the driver
   should never rely on a call to this. it can set it to NULL if
   it has nothing do do.
 */

    void (*finish)(struct _jack_driver *);\

/* the JACK engine will call this when it wishes to attach itself to
   the driver. the engine will pass a pointer to itself, which the driver
   may use in anyway it wishes to. the driver may assume that this
   is the same engine object that will make `wait' calls until a
   `detach' call is made.
 */	       

    JackDriverAttachFunction attach; \

/* the JACK engine will call this when it is finished using a driver.
 */

    JackDriverDetachFunction detach; \

/* the JACK engine will call this when it wants to wait until the 
   driver decides that its time to process some data. the driver returns
   a count of the number of audioframes that can be processed. 

   it should set the variable pointed to by `status' as follows:

   zero: the wait completed normally, processing may begin
   negative: the wait failed, and recovery is not possible
   positive: the wait failed, and the driver stopped itself.
	       a call to `start' will return the driver to	
	       a correct and known state.

   the driver should also fill out the `delayed_usecs' variable to
   indicate any delay in its expected periodic execution. for example,
   if it discovers that its return from poll(2) is later than it
   expects it to be, it would place an estimate of the delay
   in this variable. the engine will use this to decide if it 
   plans to continue execution.

 */

    JackDriverWaitFunction wait;

/* the JACK engine will call this to ask the driver to move
   data from its inputs to its output port buffers. it should
   return 0 to indicate successful completion, negative otherwise. 
 
   this function will always be called after the wait function (above).
*/

    JackDriverReadFunction read;

/* the JACK engine will call this to ask the driver to move
   data from its input port buffers to its outputs. it should
   return 0 to indicate successful completion, negative otherwise. 
 
   this function will always be called after the read function (above).
*/
    JackDriverWriteFunction write;

/* the JACK engine will call this after the wait function (above) has
   been called, but for some reason the engine is unable to execute
   a full "cycle". the driver should do whatever is necessary to
   keep itself running correctly, but cannot reference ports
   or other JACK data structures in any way.
*/

    JackDriverNullCycleFunction null_cycle;
    
/* the engine will call this when it plans to stop calling the `wait'
   function for some period of time. the driver should take
   appropriate steps to handle this (possibly no steps at all)
 */

    JackDriverStartFunction stop; \

/* the engine will call this to let the driver know that it plans
   to start calling the `wait' function on a regular basis. the driver
   should take any appropriate steps to handle this (possibly no steps
   at all)
 */

    JackDriverStopFunction start;

#else

#define JACK_DRIVER_DECL \
    jack_time_t period_usecs; \
    jack_time_t last_wait_ust; \
    void *handle; \
    void (*finish)(struct _jack_driver *);\
    JackDriverAttachFunction attach; \
    JackDriverDetachFunction detach; \
    JackDriverWaitFunction wait; \
    JackDriverReadFunction read; \
    JackDriverWriteFunction write; \
    JackDriverNullCycleFunction null_cycle; \
    JackDriverStartFunction stop; \
    JackDriverStopFunction start;

#endif /* _ANNOTATED_DRIVER_DECLARATION_ */

typedef struct _jack_driver {

    JACK_DRIVER_DECL

} jack_driver_t;

void jack_driver_init (jack_driver_t *);
void jack_driver_release (jack_driver_t *);

jack_driver_t *jack_driver_load (int argc, char **argv);
void jack_driver_unload (jack_driver_t *);

#endif /* __jack_driver_h__ */
