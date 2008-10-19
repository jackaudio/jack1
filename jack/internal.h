/* -*- mode: c; c-file-style: "bsd"; -*- */
/*
    Internal shared data and functions.

    If you edit this file, you should carefully consider changing the
    JACK_PROTOCOL_VERSION in configure.in.

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

*/

#ifndef __jack_internal_h__
#define __jack_internal_h__

#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/time.h>

#ifndef POST_PACKED_STRUCTURE
#ifdef __GNUC__
/* POST_PACKED_STRUCTURE needs to be a macro which
   expands into a compiler directive. The directive must
   tell the compiler to arrange the preceding structure
   declaration so that it is packed on byte-boundaries rather 
   than use the natural alignment of the processor and/or
   compiler.
*/
#define POST_PACKED_STRUCTURE __attribute__((__packed__))
#else
/* Add other things here for non-gcc platforms */
#endif
#endif

/* Needed by <sysdeps/time.h> */
extern void jack_error (const char *fmt, ...);

extern void jack_info (const char *fmt, ...);

#include <jack/jack.h>
#include <jack/types.h>
#include <jack/port.h>
#include <jack/transport.h>

typedef enum {
	JACK_TIMER_SYSTEM_CLOCK,
	JACK_TIMER_CYCLE_COUNTER,
	JACK_TIMER_HPET,
} jack_timer_type_t;

void        jack_init_time ();
void        jack_set_clock_source (jack_timer_type_t);
const char* jack_clock_source_name (jack_timer_type_t);

#include <sysdeps/time.h>
#include <sysdeps/atomicity.h>

#ifdef JACK_USE_MACH_THREADS
#include <sysdeps/mach_port.h>
#endif

#include <jack/messagebuffer.h>

#ifdef DEBUG_ENABLED

/* grab thread id instead of PID on linux */
#if defined(__gnu_linux__)
    #ifdef gettid  /* glibc has a version */
        #define GETTID() gettid()
    #else /* use our own version */
        #include <sys/syscall.h>
        #define GETTID() syscall(__NR_gettid)
    #endif
#else
    #define GETTID() getpid()
#endif

#define DEBUG(format,args...) \
	MESSAGE("jack:%5d:%" PRIu64 " %s:%s:%d: " format "", GETTID(), jack_get_microseconds(), __FILE__, __FUNCTION__, __LINE__ , ## args)

#else
#if JACK_CPP_VARARGS_BROKEN
    #define DEBUG(format...)
#else
    #define DEBUG(format,args...)
#endif
#endif

/* Enable preemption checking for Linux Realtime Preemption kernels.
 *
 * This checks if any RT-safe code section does anything to cause CPU
 * preemption.  Examples are sleep() or other system calls that block.
 * If a problem is detected, the kernel writes a syslog entry, and
 * sends SIGUSR2 to the client.
 */
#ifdef DO_PREEMPTION_CHECKING
#define CHECK_PREEMPTION(engine, onoff) \
	if ((engine)->real_time) gettimeofday (1, (onoff))
#else
#define CHECK_PREEMPTION(engine, onoff)
#endif

#ifndef	FALSE
#define	FALSE	(0)
#endif

#ifndef	TRUE
#define	TRUE	(1)
#endif

typedef struct _jack_engine  jack_engine_t;
typedef struct _jack_request jack_request_t;

typedef void * dlhandle;

typedef enum {
    TransportCommandNone = 0,
    TransportCommandStart = 1,
    TransportCommandStop = 2,
} transport_command_t;

typedef struct {

	volatile uint32_t       guard1;
	volatile jack_nframes_t frames;  
	volatile jack_time_t    current_wakeup;
	volatile jack_time_t    next_wakeup;
	volatile float          second_order_integrator;
	volatile int32_t        initialized;
	volatile uint32_t       guard2;
	
	/* not accessed by clients */

	int32_t  reset_pending;      /* xrun happened, deal with it */
	float    filter_coefficient; /* set once, never altered */

} POST_PACKED_STRUCTURE jack_frame_timer_t;

/* JACK engine shared memory data structure. */
typedef struct {

    jack_transport_state_t transport_state;
    volatile transport_command_t transport_cmd;
    transport_command_t	  previous_cmd;	/* previous transport_cmd */
    jack_position_t	  current_time;	/* position for current cycle */
    jack_position_t	  pending_time;	/* position for next cycle */
    jack_position_t	  request_time;	/* latest requested position */
    jack_unique_t	  prev_request; /* previous request unique ID */
    volatile _Atomic_word seq_number;	/* unique ID sequence number */
    int8_t		  new_pos;	/* new position this cycle */
    int8_t		  pending_pos;	/* new position request pending */
    jack_nframes_t	  pending_frame; /* pending frame number */
    int32_t		  sync_clients;	/* number of active_slowsync clients */
    int32_t		  sync_remain;	/* number of them with sync_poll */
    jack_time_t           sync_timeout;
    jack_time_t           sync_time_left;
    jack_frame_timer_t    frame_timer;
    int32_t		  internal;
    jack_timer_type_t     clock_source;
    pid_t                 engine_pid;
    jack_nframes_t	  buffer_size;
    int8_t		  real_time;
    int8_t		  do_mlock;
    int8_t		  do_munlock;
    int32_t		  client_priority;
    int32_t		  has_capabilities;
    float                 cpu_load;
    float		  xrun_delayed_usecs;
    float		  max_delayed_usecs;
    uint32_t		  port_max;
    int32_t		  engine_ok;
    jack_port_type_id_t	  n_port_types;
    jack_port_type_info_t port_types[JACK_MAX_PORT_TYPES];
    jack_port_shared_t    ports[0];

} POST_PACKED_STRUCTURE jack_control_t;

typedef enum  {
  BufferSizeChange,
  SampleRateChange,
  AttachPortSegment,
  PortConnected,
  PortDisconnected,
  GraphReordered,
  PortRegistered,
  PortUnregistered,
  XRun,
  StartFreewheel,
  StopFreewheel,
  ClientRegistered,
  ClientUnregistered
} JackEventType;

typedef struct {
    JackEventType type;
    union {
	uint32_t n;
        char name[JACK_CLIENT_NAME_SIZE];    
	jack_port_id_t port_id;
	jack_port_id_t self_id;
    } x;
    union {
	uint32_t n;
	jack_port_type_id_t ptid;
	jack_port_id_t other_id;
    } y;
} POST_PACKED_STRUCTURE jack_event_t;

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

/* JACK client shared memory data structure. */
typedef volatile struct {

    volatile jack_client_id_t id;         /* w: engine r: engine and client */
    volatile jack_nframes_t  nframes;     /* w: engine r: client */
    volatile jack_client_state_t state;   /* w: engine and client r: engine */
    volatile char	name[JACK_CLIENT_NAME_SIZE];
    volatile ClientType type;             /* w: engine r: engine and client */
    volatile int8_t     active;           /* w: engine r: engine and client */
    volatile int8_t     dead;             /* r/w: engine */
    volatile int8_t	timed_out;        /* r/w: engine */
    volatile int8_t     is_timebase;	  /* w: engine, r: engine and client */
    volatile int8_t     timebase_new;	  /* w: engine and client, r: engine */
    volatile int8_t     is_slowsync;	  /* w: engine, r: engine and client */
    volatile int8_t     active_slowsync;  /* w: engine, r: engine and client */
    volatile int8_t     sync_poll;        /* w: engine and client, r: engine */
    volatile int8_t     sync_new;         /* w: engine and client, r: engine */
    volatile pid_t      pid;              /* w: client r: engine; client pid */
    volatile pid_t      pgrp;             /* w: client r: engine; client pgrp */
    volatile uint64_t	signalled_at;
    volatile uint64_t	awake_at;
    volatile uint64_t	finished_at;
    volatile int32_t	last_status;         /* w: client, r: engine and client */

    /* indicators for whether callbacks have been set for this client.
       We do not include ptrs to the callbacks here (or their arguments)
       so that we can avoid 32/64 bit pointer size mismatches between
       the jack server and a client. The pointers are in the client-
       local structure which is part of the libjack compiled for
       either 32 bit or 64 bit clients.
     */
    volatile uint8_t	process_cbset;
    volatile uint8_t	thread_init_cbset;
    volatile uint8_t	bufsize_cbset;
    volatile uint8_t	srate_cbset;
    volatile uint8_t	port_register_cbset;
    volatile uint8_t	port_connect_cbset;
    volatile uint8_t	graph_order_cbset;
    volatile uint8_t	xrun_cbset;
    volatile uint8_t	sync_cb_cbset;
    volatile uint8_t	timebase_cb_cbset;
    volatile uint8_t	freewheel_cb_cbset;
    volatile uint8_t	client_register_cbset;
    volatile uint8_t	thread_cb_cbset;

} POST_PACKED_STRUCTURE jack_client_control_t;

typedef struct {
    
    uint32_t	protocol_v;		/* protocol version, must go first */
    int32_t    load;
    ClientType type;
    jack_options_t options;

    char name[JACK_CLIENT_NAME_SIZE];
    char object_path[PATH_MAX+1];
    char object_data[1024];

} POST_PACKED_STRUCTURE jack_client_connect_request_t;

typedef struct {

    jack_status_t status;

    jack_shm_registry_index_t client_shm_index;
    jack_shm_registry_index_t engine_shm_index;

    char	fifo_prefix[PATH_MAX+1];

    int32_t	realtime;
    int32_t	realtime_priority;

    char name[JACK_CLIENT_NAME_SIZE];	/* unique name, if assigned */

    /* these two are valid only for internal clients, and thus
       are exempt from the requirement that we not export
       pointers back to clients. an internal client must
       necessarily match the host, so 32/64 bit issues
       do not apply to these pointers.
    */
    jack_client_control_t* client_control;
    jack_control_t* engine_control;

#ifdef JACK_USE_MACH_THREADS
    /* specific resources for server/client real-time thread communication */
    int32_t	portnum;
#endif

} POST_PACKED_STRUCTURE jack_client_connect_result_t;

typedef struct {
    jack_client_id_t client_id;
} POST_PACKED_STRUCTURE jack_client_connect_ack_request_t;

typedef struct {
    int8_t status;
} POST_PACKED_STRUCTURE jack_client_connect_ack_result_t;

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
	ResetTimeBaseClient = 12,
	SetSyncClient = 13,
	ResetSyncClient = 14,
	SetSyncTimeout = 15,
	SetBufferSize = 16,
	FreeWheel = 17,
	StopFreeWheel = 18,
	IntClientHandle = 19,
	IntClientLoad = 20,
	IntClientName = 21,
	IntClientUnload = 22,
	RecomputeTotalLatencies = 23,
	RecomputeTotalLatency = 24
} RequestType;

struct _jack_request {
    
    //RequestType type;
    uint32_t type;
    union {
	struct {
	    char name[JACK_PORT_NAME_SIZE];
	    char type[JACK_PORT_TYPE_SIZE];
	    uint32_t         flags;
	    jack_shmsize_t   buffer_size;
	    jack_port_id_t   port_id;
	    jack_client_id_t client_id;
	} POST_PACKED_STRUCTURE port_info;
	struct {
	    char source_port[JACK_PORT_NAME_SIZE];
	    char destination_port[JACK_PORT_NAME_SIZE];
	} POST_PACKED_STRUCTURE connect;
	struct {
	    int32_t nports;
	    const char **ports;	/* this is only exposed to internal clients, so there
				   is no 64/32 issue. external clients read the ports
				   one by one from the server, and allocate their
				   own "ports" array in their own address space.
				*/
	} POST_PACKED_STRUCTURE port_connections;
	struct {
	    jack_client_id_t client_id;
	    int32_t conditional;
	} POST_PACKED_STRUCTURE timebase;
	struct {
	    //jack_options_t options;
	    uint32_t options;
	    jack_client_id_t id;
	    char name[JACK_CLIENT_NAME_SIZE];
	    char path[PATH_MAX+1];
	    char init[JACK_LOAD_INIT_LIMIT];
	} POST_PACKED_STRUCTURE intclient;
	jack_client_id_t client_id;
	jack_nframes_t nframes;
	jack_time_t timeout;
        pid_t cap_pid;
    } POST_PACKED_STRUCTURE x;
    int32_t status;
} POST_PACKED_STRUCTURE;

/* Per-client structure allocated in the server's address space.
 * It's here because its not part of the engine structure.
 */

typedef struct _jack_client_internal {

    jack_client_control_t *control;

    int        request_fd;
    int        event_fd;
    int        subgraph_start_fd;
    int        subgraph_wait_fd;
    JSList    *ports;    /* protected by engine->client_lock */
    JSList    *truefeeds;    /* protected by engine->client_lock */
    JSList    *sortfeeds;    /* protected by engine->client_lock */
    int	       fedcount;
    int	       tfedcount;
    jack_shm_info_t control_shm;
    unsigned long execution_order;
    struct  _jack_client_internal *next_client; /* not a linked list! */
    dlhandle handle;
    int     (*initialize)(jack_client_t*, const char*); /* int. clients only */
    void    (*finish)(void *);		/* internal clients only */
    int      error;
    
#ifdef JACK_USE_MACH_THREADS
    /* specific resources for server/client real-time thread communication */
    mach_port_t serverport;
    trivial_message message;
    int running;
    int portnum;
#endif /* JACK_USE_MACH_THREADS */
   
#if 0
    /* callbacks 
     */
    JackProcessCallback process;
    void *process_arg;
    JackThreadInitCallback thread_init;
    void *thread_init_arg;
    JackBufferSizeCallback bufsize;
    void *bufsize_arg;
    JackSampleRateCallback srate;
    void *srate_arg;
    JackPortRegistrationCallback port_register;
    void *port_register_arg;
    JackPortConnectCallback port_connect;
    void *port_connect_arg;
    JackGraphOrderCallback graph_order;
    void *graph_order_arg;
    JackXRunCallback xrun;
    void *xrun_arg;
    JackSyncCallback sync_cb;
    void *sync_arg;
    JackTimebaseCallback timebase_cb;
    void *timebase_arg;
    JackFreewheelCallback freewheel_cb;
    void *freewheel_arg;
    JackClientRegistrationCallback client_register;	
    void *client_register_arg;
	JackThreadCallback thread_cb;	
    void *thread_cb_arg;
#endif
    /* external clients: set by libjack
     * internal clients: set by engine */
    //int (*deliver_request)(void*, jack_request_t*); /* JOQ: 64/32 bug! */
    //void *deliver_arg;
    jack_client_t *private_client;
} jack_client_internal_t;

typedef struct _jack_thread_arg {
	jack_client_t* client;
	void* (*work_function)(void*);
	int priority;
	int realtime;
	void* arg;
	pid_t cap_pid;
} jack_thread_arg_t;

extern int  jack_client_handle_port_connection (jack_client_t *client,
						jack_event_t *event);
extern jack_client_t *jack_driver_client_new (jack_engine_t *,
					      const char *client_name);
extern jack_client_t *jack_client_alloc_internal (jack_client_control_t*,
						  jack_engine_t*);

/* internal clients call this. it's defined in jack/engine.c */
void handle_internal_client_request (jack_control_t*, jack_request_t*);

extern char *jack_tmpdir;

extern char *jack_user_dir (void);

extern char *jack_server_dir (const char *server_name, char *server_dir);

extern void *jack_zero_filled_buffer;

extern jack_port_functions_t jack_builtin_audio_functions;

extern jack_port_type_info_t jack_builtin_port_types[];

extern void jack_client_invalidate_port_buffers (jack_client_t *client);

extern void jack_transport_copy_position (jack_position_t *from,
					  jack_position_t *to);
extern void jack_call_sync_client (jack_client_t *client);

extern void jack_call_timebase_master (jack_client_t *client);

extern char *jack_default_server_name (void);

void silent_jack_error_callback (const char *desc);

/* needed for port management */
jack_port_t *jack_port_by_id_int (const jack_client_t *client,
				  jack_port_id_t id, int* free);

jack_port_t *jack_port_by_name_int (jack_client_t *client,
				    const char *port_name);
int jack_port_name_equals (jack_port_shared_t* port, const char* target);

#ifdef __GNUC__
#  define likely(x)	__builtin_expect((x),1)
#  define unlikely(x)	__builtin_expect((x),0)
#else
#  define likely(x)	(x)
#  define unlikely(x)	(x)
#endif

#endif /* __jack_internal_h__ */

