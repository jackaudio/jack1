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

#ifndef __jack_types_h__
#define __jack_types_h__

#include <limits.h> /* ULONG_MAX */

/**
 * Type used to represent sample frame counts.
 */
typedef unsigned long        jack_nframes_t;

/**
 * Maximum value that can be stored in jack_nframes_t
 */
#define JACK_MAX_FRAMES ULONG_MAX;

/**
 *  jack_port_t is an opaque type. You may only access it using the API provided.
 */
typedef struct _jack_port    jack_port_t;

/**
 *  jack_client_t is an opaque type. You may only access it using the API provided.
 */
typedef struct _jack_client  jack_client_t;

/**
 *  Ports have unique ids. You will very rarely need to know them, however,
 *  except in the case of the port registration callback.
 */
typedef long                 jack_port_id_t;

/**
 * Prototype for the client supplied function that is called 
 * by the engine anytime there is work to be done.
 *
 * @pre nframes == jack_get_buffer_size()
 *
 * @param nframes number of frames to process
 * @param arg pointer to a client supplied structure
 *
 * @return zero on success, non-zero on error
 */ 
typedef int  (*JackProcessCallback)(jack_nframes_t nframes, void *arg);

/**
 * Prototype for the client supplied function that is called 
 * whenever the processing graph is reordered.
 *
 * @param arg pointer to a client supplied structure
 *
 * @return zero on success, non-zero on error
 */ 
typedef int  (*JackGraphOrderCallback)(void *arg);

/**
 * Prototype for the client supplied function that is called 
 * whenever an xrun has occured.
 *
 * @param arg pointer to a client supplied structure
 *
 * @return zero on success, non-zero on error
 */ 
typedef int  (*JackXRunCallback)(void *arg);

/**
 * Prototype for the client supplied function that is called 
 * when the engine buffersize changes.
 *
 * Note! Use of this callback function is deprecated!
 *
 * @param nframes new engine buffer size
 * @param arg pointer to a client supplied structure
 *
 * @return zero on success, non-zero on error
 */ 
typedef int  (*JackBufferSizeCallback)(jack_nframes_t nframes, void *arg);

/**
 * Prototype for the client supplied function that is called 
 * when the engine sample rate changes.
 *
 * @param nframes new engine sample rate
 * @param arg pointer to a client supplied structure
 *
 * @return zero on success, non-zero on error
 */ 
typedef int  (*JackSampleRateCallback)(jack_nframes_t nframes, void *arg);

/**
 * Prototype for the client supplied function that is called 
 * whenever a port is registered or unregistered.
 *
 * @param arg pointer to a client supplied structure
 */ 
typedef void (*JackPortRegistrationCallback)(jack_port_id_t port, int, void *arg);

/**
 * Used for the type argument of jack_port_register().
 */
#define JACK_DEFAULT_AUDIO_TYPE "32 bit float mono audio"

/**
 * For convenience, use this typedef if you want to be able to change
 * between float and double. You may want to typedef sample_t to
 * jack_default_audio_sample_t in your application.
 */
typedef float jack_default_audio_sample_t;

/**
 *  A port has a set of flags that are formed by AND-ing together the
 *  desired values from the list below. The flags "JackPortIsInput" and
 *  "JackPortIsOutput" are mutually exclusive and it is an error to use
 *  them both.
 */
enum JackPortFlags {

     /**
      * if JackPortIsInput is set, then the port can receive
      * data.
      */
     JackPortIsInput = 0x1,

     /**
      * if JackPortIsOutput is set, then data can be read from
      * the port.
      */
     JackPortIsOutput = 0x2,

     /**
      * if JackPortIsPhysical is set, then the port corresponds
      * to some kind of physical I/O connector.
      */
     JackPortIsPhysical = 0x4, 

     /**
      * if JackPortCanMonitor is set, then a call to
      * jack_port_request_monitor() makes sense.
      *
      * Precisely what this means is dependent on the client. A typical
      * result of it being called with TRUE as the second argument is
      * that data that would be available from an output port (with
      * JackPortIsPhysical set) is sent to a physical output connector
      * as well, so that it can be heard/seen/whatever.
      * 
      * Clients that do not control physical interfaces
      * should never create ports with this bit set.
      */
     JackPortCanMonitor = 0x8,

     /**
      * JackPortIsTerminal means:
      *
      *	for an input port: the data received by the port
      *                    will not be passed on or made
      *		           available at any other port
      *
      * for an output port: the data available at the port
      *                    does not originate from any other port
      *
      * Audio synthesizers, i/o h/w interface clients, HDR
      * systems are examples of things that would set this
      * flag for their ports.  
      */
     JackPortIsTerminal = 0x10
};	    

#endif /* __jack_types_h__ */
