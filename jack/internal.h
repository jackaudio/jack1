/*
    Copyright (C) 2001-2003 Paul Davis
    
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

#include <jack/jack.h>
#include <jack/types.h>
#include <jack/port.h>
#include <jack/transport.h>
#include <jack/time.h>

#ifdef DEBUG_ENABLED
#define DEBUG(format,args...) \
	printf ("jack:%5d:%Lu %s:%s:%d: " format "\n", getpid(), jack_get_microseconds(), __FILE__, __FUNCTION__, __LINE__ , ## args)
#else
#define DEBUG(format,args...)
#endif

#ifndef	FALSE
#define	FALSE	(0)
#endif

#ifndef	TRUE
#define	TRUE	(!FALSE)
#endif

typedef struct _jack_engine  jack_engine_t;
typedef struct _jack_request jack_request_t;

typedef void * dlhandle;

typedef struct {
    const char *shm_name;
    size_t offset;
} jack_port_buffer_info_t;

typedef struct {
    volatile jack_time_t guard1;
    volatile jack_nframes_t frames;
    volatile jack_time_t stamp;
    volatile jack_time_t guard2;
} jack_frame_timer_t;

/* the relatively low value of this constant
 * reflects the fact that JACK currently only
 * knows about *1* port type.  (March 2003)
 */              

#define JACK_MAX_PORT_TYPES 4

typedef struct {

    jack_transport_info_t current_time;
    jack_transport_info_t pending_time;
    jack_frame_timer_t    frame_timer;
    int                   internal;
    jack_nframes_t        frames_at_cycle_start;
    pid_t                 engine_pid;
    unsigned long         buffer_size;
    char                  real_time;
    int                   client_priority;
    int                   has_capabilities;
    float                 cpu_load;
    unsigned long         port_max;
    int                   engine_ok;
    jack_engine_t        *engine;
    unsigned long         n_port_types;
    jack_port_type_info_t port_types[JACK_MAX_PORT_TYPES];
    jack_port_shared_t  ports[0];

} jack_control_t;

typedef enum  {
  BufferSizeChange,
  SampleRateChange,
  NewPortType,
  PortConnected,
  PortDisconnected,
  GraphReordered,
  PortRegistered,
  PortUnregistered,
  XRun,
} EventType;

typedef struct {
    EventType type;
    union {
	unsigned long n;
	jack_port_id_t port_id;
	jack_port_id_t self_id;
	shm_name_t shm_name;
    } x;
    union {
	unsigned long n;
	jack_port_id_t other_id;
	void* addr;
    } y;
    union { 
	size_t size;
    } z;
} jack_event_t;

typedef enum {
	ClientInternal, /* connect request just names .so */
	ClientDriver,   /* code is loaded along with driver */
	ClientExternal  /* client is in another process */
} ClientType;

typedef enum {
	NotTriggered,
	Triggered,
	Running,
	Finished
} jack_client_state_t;

typedef volatile struct {

    volatile jack_client_id_t id;              /* w: engine r: engine and client */
    volatile jack_nframes_t  nframes;          /* w: engine r: client */
    volatile jack_client_state_t state;   /* w: engine and client r: engine */
    volatile char       name[JACK_CLIENT_NAME_SIZE+1];
    volatile ClientType type;             /* w: engine r: engine and client */
    volatile char       active : 1;       /* w: engine r: engine and client */
    volatile char       dead : 1;         /* r/w: engine */
    volatile char       timed_out : 1;    /* r/w: engine */
    volatile pid_t      pid;              /* w: client r: engine; pid of client */
    volatile unsigned long long signalled_at;
    volatile unsigned long long awake_at;
    volatile unsigned long long finished_at;

    /* callbacks */
    
    JackProcessCallback process;
    void *process_arg;
    JackBufferSizeCallback bufsize;
    void *bufsize_arg;
    JackSampleRateCallback srate;
    void *srate_arg;
    JackPortRegistrationCallback port_register;
    void *port_register_arg;
    JackGraphOrderCallback graph_order;
    void *graph_order_arg;
    JackXRunCallback xrun;
    void *xrun_arg;

    /* OOP clients: set by libjack
        IP clients: set by engine
    */
    
    int (*deliver_request)(void*, jack_request_t*);
    void *deliver_arg;

    /* for engine use only */

    void *private_client;

} jack_client_control_t;

typedef struct {
    
    int        load;
    ClientType type;

    char name[JACK_CLIENT_NAME_SIZE+1];
    char object_path[PATH_MAX+1];
    char object_data[1024];

} jack_client_connect_request_t;

typedef struct {

    int status;

    unsigned int protocol_v;

    shm_name_t client_shm_name;
    shm_name_t control_shm_name;

    char fifo_prefix[PATH_MAX+1];

    int realtime;
    int realtime_priority;

    /* these two are valid only if the connect request
       was for type == ClientDriver.
    */

    jack_client_control_t *client_control;
    jack_control_t        *engine_control;
    size_t                 control_size;

    /* when we write this response, we deliver n_port_types
       of jack_port_type_info_t after it.
    */

    unsigned long n_port_types;

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
	ActivateClient = 6,
	DeactivateClient = 7,
	DisconnectPort = 8,
	SetClientCapabilities = 9,
	GetPortConnections = 10,
	GetPortNConnections = 11,
} RequestType;

struct _jack_request {
    
    RequestType type;
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
	struct {
	    unsigned int nports;
	    const char **ports;  
	} port_connections;
	jack_client_id_t client_id;
	jack_nframes_t nframes;
    } x;
    int status;
};

extern void jack_cleanup_files ();

extern int  jack_client_handle_port_connection (jack_client_t *client, jack_event_t *event);
extern void jack_client_handle_new_port_type (jack_client_t *client, shm_name_t, size_t, void* addr);

extern jack_client_t *jack_driver_client_new (jack_engine_t *, const char *client_name);
jack_client_t *jack_client_alloc_internal (jack_client_control_t*, jack_control_t*);

/* internal clients call this. its defined in jack/engine.c */

void handle_internal_client_request (jack_control_t*, jack_request_t*);

extern char *jack_server_dir;

extern void jack_error (const char *fmt, ...);

extern jack_port_type_info_t jack_builtin_port_types[];

extern void jack_client_invalidate_port_buffers (jack_client_t *client);

#endif /* __jack_internal_h__ */




