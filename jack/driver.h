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

typedef void (*ClockSyncListenerFunction)(channel_t,ClockSyncStatus,void*);

typedef struct {
    unsigned long id;
    ClockSyncListenerFunction function;
    void *arg;
} ClockSyncListener;

typedef void (*InputMonitorListenerFunction)(channel_t,int,void*);

typedef struct {
    unsigned long id;
    InputMonitorListenerFunction function;
    void *arg;
} InputMonitorListener;

struct _jack_engine;
struct _jack_driver;

typedef int            (*JackDriverAttachFunction)(struct _jack_driver *, struct _jack_engine *);
typedef int             (*JackDriverDetachFunction)(struct _jack_driver *, struct _jack_engine *);
typedef int             (*JackDriverWaitFunction)(struct _jack_driver *);
typedef nframes_t       (*JackDriverFramesSinceCycleStartFunction)(struct _jack_driver *);
typedef ClockSyncStatus (*JackDriverClockSyncStatusFunction)(struct _jack_driver *, channel_t);
typedef int             (*JackDriverAudioStopFunction)(struct _jack_driver *);
typedef int             (*JackDriverAudioStartFunction)(struct _jack_driver *);
typedef void            (*JackDriverSetHwMonitoringFunction) (struct _jack_driver *, int);
typedef int             (*JackDriverChangeSampleClockFunction) (struct _jack_driver *, SampleClockMode mode);
typedef int             (*JackDriverResetParametersFunction) (struct _jack_driver *, nframes_t frames_per_cycle, nframes_t rate);
typedef void            (*JackDriverMarkChannelSilentFunction) (struct _jack_driver *, unsigned long chn);
typedef void            (*JackDriverRequestMonitorInputFunction) (struct _jack_driver *, unsigned long chn, int yn);
typedef void            (*JackDriverRequestAllMonitorInputFunction) (struct _jack_driver *, int yn);
typedef int             (*JackDriverMonitoringInputFunction)(struct _jack_driver *,channel_t chn);

#define JACK_DRIVER_DECL \
    nframes_t frame_rate; \
    nframes_t frames_per_cycle; \
    nframes_t capture_frame_latency; \
    nframes_t playback_frame_latency; \
    unsigned long period_interval; \
    SampleClockMode clock_mode; \
    unsigned long input_monitor_mask; \
    struct _jack_engine *engine; \
    GSList *clock_sync_listeners; \
    pthread_mutex_t clock_sync_lock; \
    unsigned long next_clock_sync_listener_id; \
    GSList *input_monitor_listeners; \
    pthread_mutex_t input_monitor_lock; \
    unsigned long next_input_monitor_listener_id; \
    char hw_monitoring : 1; \
    char all_monitor_in : 1; \
    char has_clock_sync_reporting : 1; \
    char has_hw_monitoring : 1; \
    void *handle; \
    void (*finish)(struct _jack_driver *);\
\
    /* These are the "core" driver functions */ \
\
    JackDriverAttachFunction attach; \
    JackDriverDetachFunction detach; \
    JackDriverWaitFunction wait; \
\
    /* These support the "audio" side of a driver, which arguably \
       could/should be provided by a different kind of object. \
    */ \
\
    JackDriverFramesSinceCycleStartFunction frames_since_cycle_start; \
    JackDriverClockSyncStatusFunction clock_sync_status; \
    JackDriverAudioStopFunction audio_stop; \
    JackDriverAudioStartFunction audio_start; \
    JackDriverSetHwMonitoringFunction set_hw_monitoring; \
    JackDriverChangeSampleClockFunction change_sample_clock; \
    JackDriverResetParametersFunction reset_parameters; \
    JackDriverMarkChannelSilentFunction mark_channel_silent; \
    JackDriverRequestMonitorInputFunction request_monitor_input; \
    JackDriverRequestAllMonitorInputFunction request_all_monitor_input; \
    JackDriverMonitoringInputFunction monitoring_input;

typedef struct _jack_driver {

    JACK_DRIVER_DECL

} jack_driver_t;

void jack_driver_init (jack_driver_t *);
void jack_driver_release (jack_driver_t *);

jack_driver_t *jack_driver_load (const char *path_to_so, ...);
void jack_driver_unload (jack_driver_t *);

int jack_driver_listen_for_clock_sync_status (jack_driver_t *, ClockSyncListenerFunction, void *arg);
int jack_driver_stop_listen_for_clock_sync_status (jack_driver_t *, int);

int jack_driver_listen_for_input_monitor_status (jack_driver_t *, InputMonitorListenerFunction, void *arg);
int jack_driver_stop_listen_for_input_monitor_status (jack_driver_t *, int);

void jack_driver_clock_sync_notify (jack_driver_t *, channel_t chn, ClockSyncStatus);
void jack_driver_input_monitor_notify (jack_driver_t *, channel_t chn, int);

#endif /* __jack_driver_h__ */








