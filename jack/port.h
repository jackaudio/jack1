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

#ifndef __jack_port_h__
#define __jack_port_h__

#include <pthread.h>
#include <jack/types.h>
#include <jack/jslist.h>
#include <jack/shm.h>

#define JACK_PORT_NAME_SIZE 32
#define JACK_PORT_TYPE_SIZE 32

/* The relatively low value of this constant reflects the fact that
 * JACK currently only knows about *1* port type.  (March 2003)
 *
 * Further, the 4 covers:
 *   - a single non-negotiated audio format
 *   - music data (ie. MIDI)
 *   - video
 *   - one other
 *
 * which is probably enough for more than just the foreseeable future.
 */              
#define JACK_MAX_PORT_TYPES 4
#define JACK_AUDIO_PORT_TYPE 0

/* these should probably go somewhere else, but not in <jack/types.h> */
#define JACK_CLIENT_NAME_SIZE 32
typedef uint32_t jack_client_id_t;

/* JACK shared memory segments are limited to MAX_INT32, they can be
 * shared between 32-bit and 64-bit clients. 
 */
#define JACK_SHM_MAX (MAX_INT32)
typedef int32_t jack_port_type_id_t;

/* Port type structure.  
 *
 *  (1) One for each port type is part of the engine's jack_control_t
 *  shared memory structure.
 *
 *  (2) One for each port type is appended to the engine's
 *  jack_client_connect_result_t response.  The client reads them into
 *  its local memory, using them to attach the corresponding shared
 *  memory segments.
 */
typedef struct _jack_port_type_info {

    jack_port_type_id_t ptype_id;
    const char     type_name[JACK_PORT_TYPE_SIZE];      

    /* If == 1, then a buffer to handle nframes worth of data has
     * sizeof(jack_default_audio_sample_t) * nframes bytes.  
     *
     * If > 1, the buffer allocated for input mixing will be
     * this value times sizeof(jack_default_audio_sample_t)
     * * nframes bytes in size.  For non-audio data types,
     * it may have a different value.
     *
     * If < 0, the value should be ignored, and buffer_size
     * should be used.
     */
    int32_t buffer_scale_factor;

    /* ignored unless buffer_scale_factor is < 0. see above */
    jack_shmsize_t buffer_size;

    jack_shm_registry_index_t shm_registry_index;

} jack_port_type_info_t;

/* Allocated by the engine in shared memory. */
typedef struct _jack_port_shared {

    jack_port_type_id_t      ptype_id;	/* index into port type array */
    jack_shmsize_t           offset;	/* buffer offset in shm segment */
    jack_port_id_t           id;	/* index into engine port array */
    enum JackPortFlags	     flags;    
    char                     name[JACK_CLIENT_NAME_SIZE+JACK_PORT_NAME_SIZE+2];
    jack_client_id_t         client_id;	/* who owns me */

    volatile jack_nframes_t  latency;
    volatile jack_nframes_t  total_latency;
    volatile uint8_t	     monitor_requests;

    char		     has_mixdown; /* port has a mixdown function */
    char                     in_use;
    char                     locked;

} jack_port_shared_t;

typedef struct _jack_port_functions {

    /* Function to mixdown multiple inputs to a buffer.  Can be NULL,
     * indicating that multiple input connections are not legal for
     * this data type. 
     */
    void (*mixdown)(jack_port_t *, jack_nframes_t);

} jack_port_functions_t;

/* Allocated by the client in local memory. */
struct _jack_port {
    void                    **client_segment_base;
    void                     *mix_buffer;
    jack_port_type_info_t    *type_info; /* shared memory type info */
    struct _jack_port_shared *shared;	 /* corresponding shm struct */
    struct _jack_port        *tied;	 /* locally tied source port */
    jack_port_functions_t    fptr;
    pthread_mutex_t          connection_lock;
    JSList                   *connections;
};

/*  Inline would be cleaner, but it needs to be fast even in
 *  non-optimized code.  jack_output_port_buffer() only handles output
 *  ports.  jack_port_buffer() works for both input and output ports.
 */
#define jack_port_buffer(p) \
  ((void *) ((p)->mix_buffer? (p)->mix_buffer: \
   *(p)->client_segment_base + (p)->shared->offset))
#define jack_output_port_buffer(p) \
  ((void *) (*(p)->client_segment_base + (p)->shared->offset))

#endif /* __jack_port_h__ */

