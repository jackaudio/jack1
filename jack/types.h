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

#ifndef __jack_engine_types_h__
#define __jack_engine_types_h__

#include <limits.h>

typedef float         sample_t;
typedef unsigned long nframes_t;
typedef long          jack_port_id_t;
typedef unsigned long jack_client_id_t;
typedef float         gain_t;
typedef long          channel_t;

static  const nframes_t max_frames = ULONG_MAX;

typedef int  (*JackProcessCallback)(nframes_t, void *);
typedef int  (*JackBufferSizeCallback)(nframes_t, void *);
typedef int  (*JackSampleRateCallback)(nframes_t, void *);
typedef void (*JackPortRegistrationCallback)(jack_port_id_t,int,void*);

#define NoChannel -1
#define NoPort    -1

typedef struct _jack_engine  jack_engine_t;
typedef struct _jack_port    jack_port_t;
typedef struct _jack_client  jack_client_t;

#define JACK_PORT_NAME_SIZE 32
#define JACK_PORT_TYPE_SIZE 32
#define JACK_CLIENT_NAME_SIZE 32

typedef enum {
	Cap_HardwareMonitoring = 0x1,
	Cap_AutoSync = 0x2,
	Cap_WordClock = 0x4,
	Cap_ClockMaster = 0x8,
	Cap_ClockLockReporting = 0x10
} Capabilities;

typedef	enum  {
	AutoSync,
	WordClock,
	ClockMaster
} SampleClockMode;

typedef	enum  {
	Lock = 0x1,
	NoLock = 0x2,
	Sync = 0x4,
	NoSync = 0x8
} ClockSyncStatus;

#endif /* __jack_engine_types_h__ */
