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

#ifndef __jack_internal_h__
#define __jack_internal_h__

#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/time.h>
#include <pthread.h>
#include <glib.h>

#include <jack/jack.h>
#include <jack/types.h>
#include <jack/port.h>

#define FIFO_NAME_SIZE 32

typedef void * dlhandle;

typedef struct {
    int    shm_key;
    size_t offset;
} jack_port_buffer_info_t;

typedef struct {
    int    shm_key;
    char  *address;
} jack_port_segment_info_t;

typedef struct _time_info
{
    nframes_t frame;
    nframes_t frame_rate;
    unsigned long microseconds;

    double ppqPos;        // 1 ppq
    double tempo;         // in bpm
    double barStartPos;   // last bar start, in 1 ppq
    double cycleStartPos; // 1 ppq
    double cycleEndPos;   // 1 ppq

    float timeSigNumerator;  // time signature
    float timeSigDenominator;
    long smpteOffset;
    long smpteFrameRate;  // 0:24, 1:25, 2:29.97, 3:30, 4:29.97 df, 5:30 df
    long samplesToNextClock; // midi clock resolution (24 ppq), can be negative
    long flags;     // see below
} time_info;

typedef struct {

    time_info           time;
    pid_t               engine_pid;
    unsigned long       buffer_size;
    char                real_time;
    int                 client_priority;
    unsigned long       port_max;
    jack_port_shared_t ports[0];

} jack_control_t;

typedef enum  {
  BufferSizeChange,
  SampleRateChange,
  NewPortBufferSegment,
  PortConnected,
  PortDisconnected,
  GraphReordered,
  PortRegistered,
  PortUnregistered,
} AudioEngineEventType;

typedef struct {
    AudioEngineEventType type;
    union {
	unsigned long n;
	jack_port_id_t port_id;
	jack_port_id_t self_id;
    } x;
    union {
	unsigned long n;
	jack_port_id_t other_id;
    } y;
} jack_event_t;

typedef enum {
	ClientDynamic,     /* connect request just names .so */
	ClientDriver,      /* code is loaded along with driver */
	ClientOutOfProcess /* client is in another process */
} ClientType;

typedef enum {
	NotTriggered,
	Triggered,
	Running,
	Finished
} jack_client_state_t;

typedef volatile struct {

    volatile nframes_t  frame_time;       /* w: client r: engine (if client is timebase) */
    volatile int        id;               /* w: engine r: engine and client */
    volatile nframes_t  nframes;          /* w: engine r: client */
    volatile jack_client_state_t state;   /* w: engine and client r: engine */
    volatile char       name[JACK_CLIENT_NAME_SIZE+1];
    volatile ClientType type;             /* w: engine r: engine and client */
    volatile char       active : 1;       /* w: engine r: engine and client */
    volatile char       dead : 1;         /* r/w: engine */

    /* callbacks */
    
    JackProcessCallback process;
    void *process_arg;
    JackBufferSizeCallback bufsize;
    void *bufsize_arg;
    JackSampleRateCallback srate;
    void *srate_arg;
    JackPortRegistrationCallback port_register;
    void *port_register_arg;

    /* for engine use only */

    void *private_internal_client;

} jack_client_control_t;

typedef struct {
    
    ClientType type;

    char name[JACK_CLIENT_NAME_SIZE+1];
    char object_path[PATH_MAX+1];

} jack_client_connect_request_t;

typedef struct {

    int status;

    int client_key;
    int control_key;

    char fifo_prefix[FIFO_NAME_SIZE+1];

    int realtime;
    int realtime_priority;

    /* these two are valid only if the connect request
       was for type == ClientDriver.
    */

    jack_client_control_t *client_control;
    jack_control_t *engine_control;

    /* XXX need to be able to use more than one port segment key */

    key_t port_segment_key;

} jack_client_connect_result_t;

typedef struct {
    jack_client_id_t client_id;
} jack_client_connect_ack_request_t;

typedef struct {
    char status;
} jack_client_connect_ack_result_t;

typedef enum {
	RegisterPort = 1,
	UnRegisterPort = 2,
	ConnectPorts = 3,
	DisconnectPorts = 4, 
	SetTimeBaseClient = 5,
	DropClient = 6,
	ActivateClient = 7,
	DeactivateClient = 8,
	GetPortTotalLatency = 9,
	DisconnectPort = 10
} AudioEngineRequestType;

typedef struct {
    
    AudioEngineRequestType type;
    union {
	struct {
	    char name[JACK_PORT_NAME_SIZE+1];
	    char type[JACK_PORT_TYPE_SIZE+1];
	    unsigned long flags;
	    unsigned long buffer_size;
	    jack_port_id_t port_id;
	    jack_client_id_t client_id;
	} port_info;
	struct {
	    char source_port[JACK_PORT_NAME_SIZE+1];
	    char destination_port[JACK_PORT_NAME_SIZE+1];
	} connect;
	jack_client_id_t client_id;
	nframes_t nframes;
    } x;
    int status;
}  jack_request_t;

extern void jack_cleanup_shm ();
extern void jack_cleanup_files ();

extern int jack_client_handle_port_connection (jack_client_t *client, jack_event_t *event);

jack_client_t *jack_driver_become_client (const char *client_name);

extern char *jack_temp_dir;

#endif /* __jack_internal_h__ */




