/*
    Copyright (C) 2003 Robert Ham <rah@bash.sh>

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
*/


#ifndef __JACK_DUMMY_DRIVER_H__
#define __JACK_DUMMY_DRIVER_H__

#include <unistd.h>

#include <jack/types.h>
#include <jack/jslist.h>
#include <jack/driver.h>
#include <jack/jack.h>
#include <config.h>

// needed for clock_nanosleep
#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif
#include <time.h>

typedef struct _dummy_driver dummy_driver_t;

struct _dummy_driver
{
    JACK_DRIVER_NT_DECL;

    jack_nframes_t  sample_rate;
    jack_nframes_t  period_size;
    unsigned long   wait_time;

#ifdef HAVE_CLOCK_GETTIME
    struct timespec next_wakeup;
#else
    jack_time_t     next_time;
#endif

    unsigned int    capture_channels;
    unsigned int    playback_channels;

    JSList	   *capture_ports;
    JSList	   *playback_ports;

    jack_client_t  *client;
};

#endif /* __JACK_DUMMY_DRIVER_H__ */
