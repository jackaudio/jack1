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

#include <glib.h>
#include <pthread.h>
#include <jack/types.h>
#include <jack/port.h>

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
typedef nframes_t (*JackDriverWaitFunction)(struct _jack_driver *, int fd, int *status, float *delayed_usecs);
typedef int       (*JackDriverProcessFunction)(struct _jack_driver *, nframes_t);
typedef int       (*JackDriverStopFunction)(struct _jack_driver *);
typedef int       (*JackDriverStartFunction)(struct _jack_driver *);

#define JACK_DRIVER_DECL \
    nframes_t period_usecs; \
    void *handle; \
    void (*finish)(struct _jack_driver *);\
    JackDriverAttachFunction attach; \
    JackDriverDetachFunction detach; \
    JackDriverWaitFunction wait; \
    JackDriverProcessFunction process; \
    JackDriverStartFunction stop; \
    JackDriverStopFunction start;

typedef struct _jack_driver {

    JACK_DRIVER_DECL

} jack_driver_t;

void jack_driver_init (jack_driver_t *);
void jack_driver_release (jack_driver_t *);

jack_driver_t *jack_driver_load (int argc, char **argv);
void jack_driver_unload (jack_driver_t *);

#endif /* __jack_driver_h__ */








