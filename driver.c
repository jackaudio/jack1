/*
    Copyright (C) 2001 Paul Davis
    
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
    
    $Id$
*/

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include <jack/driver.h>
#include <jack/internal.h>
#include <jack/error.h>

static int dummy_attach (jack_driver_t *drv, jack_engine_t *eng) { return 0; }
static int dummy_detach (jack_driver_t *drv, jack_engine_t *eng) { return 0; }
static int dummy_wait (jack_driver_t *drv) { return 0; }
static ClockSyncStatus dummy_clock_sync_status (jack_driver_t *drv, channel_t chn) { return ClockMaster; }
static int dummy_audio_stop (jack_driver_t *drv) { return 0; }
static int dummy_audio_start (jack_driver_t *drv) { return 0;; }
static void dummy_set_hw_monitoring  (jack_driver_t *drv, int yn) { return; }
static int dummy_change_sample_clock  (jack_driver_t *drv, SampleClockMode mode) { return 0; }
static int dummy_reset_parameters  (jack_driver_t *drv, nframes_t frames_per_cycle, nframes_t rate) { return 0; }
static void dummy_mark_channel_silent  (jack_driver_t *drv, unsigned long chn) { return; }
static void dummy_request_all_monitor_input  (jack_driver_t *drv, int yn) { return; }

void
jack_driver_init (jack_driver_t *driver)

{
	memset (driver, 0, sizeof (*driver));

	driver->input_monitor_mask = 0;

	driver->attach = dummy_attach;
	driver->detach = dummy_detach;
	driver->wait = dummy_wait;
	driver->clock_sync_status = dummy_clock_sync_status;
	driver->audio_stop = dummy_audio_stop;
	driver->audio_start = dummy_audio_start;
	driver->set_hw_monitoring  = dummy_set_hw_monitoring ;
	driver->change_sample_clock  = dummy_change_sample_clock;
	driver->reset_parameters  = dummy_reset_parameters;
	driver->mark_channel_silent  = dummy_mark_channel_silent;
	driver->request_all_monitor_input  = dummy_request_all_monitor_input;
	driver->engine = 0;

	pthread_mutex_init (&driver->clock_sync_lock, 0);
	driver->clock_sync_listeners = 0;

	pthread_mutex_init (&driver->input_monitor_lock, 0);
	driver->input_monitor_listeners = 0;
}

void
jack_driver_release (jack_driver_t *driver)

{
	GSList *node;

	for (node = driver->clock_sync_listeners; node; node = g_slist_next (node)) {
		free (node->data);
	}
	g_slist_free (driver->clock_sync_listeners);

	for (node = driver->input_monitor_listeners; node; node = g_slist_next (node)) {
		free (node->data);
	}
	g_slist_free (driver->input_monitor_listeners);
}

int
jack_driver_listen_for_clock_sync_status (jack_driver_t *driver, 
					  ClockSyncListenerFunction func,
					  void *arg)
{
	ClockSyncListener *csl;

	csl = (ClockSyncListener *) malloc (sizeof (ClockSyncListener));
	csl->function = func;
	csl->arg = arg;
	csl->id = driver->next_clock_sync_listener_id++;
	
	pthread_mutex_lock (&driver->clock_sync_lock);
	driver->clock_sync_listeners = g_slist_prepend (driver->clock_sync_listeners, csl);
	pthread_mutex_unlock (&driver->clock_sync_lock);
	return csl->id;
}

int
jack_driver_stop_listening_to_clock_sync_status (jack_driver_t *driver, int which)

{
	GSList *node;
	int ret = -1;
	pthread_mutex_lock (&driver->clock_sync_lock);
	for (node = driver->clock_sync_listeners; node; node = g_slist_next (node)) {
		if (((ClockSyncListener *) node->data)->id == which) {
			driver->clock_sync_listeners = g_slist_remove_link (driver->clock_sync_listeners, node);
			free (node->data);
			g_slist_free_1 (node);
			ret = 0;
			break;
		}
	}
	pthread_mutex_unlock (&driver->clock_sync_lock);
	return ret;
}

void jack_driver_clock_sync_notify (jack_driver_t *driver, channel_t chn, ClockSyncStatus status)
{
	GSList *node;

	pthread_mutex_lock (&driver->clock_sync_lock);
	for (node = driver->clock_sync_listeners; node; node = g_slist_next (node)) {
		ClockSyncListener *csl = (ClockSyncListener *) node->data;
		csl->function (chn, status, csl->arg);
	}
	pthread_mutex_unlock (&driver->clock_sync_lock);

}

jack_driver_t *
jack_driver_load (const char *path_to_so, ...)

{
	va_list ap;
	const char *errstr;
	dlhandle handle;
	jack_driver_t *driver;
	jack_driver_t *(*initialize)(va_list);
	void (*finish)(jack_driver_t *);

	va_start (ap, path_to_so);
	handle = dlopen (path_to_so, RTLD_NOW|RTLD_GLOBAL);
	
	if (handle == 0) {
		if ((errstr = dlerror ()) != 0) {
			jack_error ("can't load \"%s\": %s", path_to_so, errstr);
		} else {
			jack_error ("bizarre error loading driver shared object %s", path_to_so);
		}
		va_end (ap);
		return 0;
	}

	initialize = dlsym (handle, "driver_initialize");

	if ((errstr = dlerror ()) != 0) {
		jack_error ("no initialize function in shared object %s\n", path_to_so);
		dlclose (handle);
		va_end (ap);
		return 0;
	}

	finish = dlsym (handle, "driver_finish");

	if ((errstr = dlerror ()) != 0) {
		jack_error ("no finish function in in shared driver object %s", path_to_so);
		dlclose (handle);
		va_end (ap);
		return 0;
	}

	if ((driver = initialize (ap)) != 0) {
		driver->handle = handle;
		driver->finish = finish;
	}

	va_end (ap);

	return driver;

}

void
jack_driver_unload (jack_driver_t *driver)
{
	driver->finish (driver);
	dlclose (driver->handle);
}
