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

#ifndef __jack_h__
#define __jack_h__

#ifdef __cplusplus
extern "C" {
#endif

#include <jack/types.h>
#include <jack/error.h>

jack_client_t *jack_client_new (const char *client_name);
int            jack_client_close (jack_client_t *client);

/* register a function (and argument) to be called if and when the
   JACK server shuts down the client thread. the function must
   be written as if it were an asynchonrous POSIX signal
   handler - use only async-safe functions, and remember that it
   is executed from another thread. a typical function might
   set a flag or write to a pipe so that the rest of the
   application knows that the JACK client thread has shut
   down.

   NOTE: clients do not need to call this. it exists only
   to help more complex clients understand what is going
   on. if called, it must be called before jack_client_activate().
*/

void jack_on_shutdown (jack_client_t *, void (*function)(void *arg), void *arg);

/* see simple_client.c to understand what these do.
 */

int jack_set_process_callback (jack_client_t *, JackProcessCallback, void *arg);
int jack_set_buffer_size_callback (jack_client_t *, JackBufferSizeCallback, void *arg);
int jack_set_sample_rate_callback (jack_client_t *, JackSampleRateCallback, void *arg);
int jack_set_port_registration_callback (jack_client_t *, JackPortRegistrationCallback, void *);
int jack_set_graph_order_callback (jack_client_t *, JackGraphOrderCallback, void *);

int jack_activate (jack_client_t *client);
int jack_deactivate (jack_client_t *client);

/* this creates a new port for the client. 

   a port is an object used for moving data in or out of the client.
   the data may be of any type. ports may be connected to each other
   in various ways.

   a port has a short name, which may be any non-NULL and non-zero
   length string, and is passed as the first argument. a port's full
   name is the name of the client concatenated with a colon (:) and
   then its short name.

   a port has a type, which may be any non-NULL and non-zero length
   string, and is passed as the second argument. for types that are
   not built into the jack API (currently just
   JACK_DEFAULT_AUDIO_TYPE) the client MUST supply a non-zero size
   for the buffer as the fourth argument. for builtin types, the
   fourth argument is ignored.

   a port has a set of flags, enumerated below and passed as the third
   argument in the form of a bitmask created by AND-ing together the
   desired flags. the flags "IsInput" and "IsOutput" are mutually
   exclusive and it is an error to use them both.  

*/

enum JackPortFlags {

     JackPortIsInput = 0x1,
     JackPortIsOutput = 0x2,
     JackPortIsPhysical = 0x4, /* refers to a physical connection */

     /* if JackPortCanMonitor is set, then a call to
	jack_port_request_monitor() makes sense.
	
	Precisely what this means is dependent on the client. A typical
	result of it being called with TRUE as the second argument is
	that data that would be available from an output port (with
	JackPortIsPhysical set) is sent to a physical output connector
	as well, so that it can be heard/seen/whatever.
	
	Clients that do not control physical interfaces
	should never create ports with this bit set.
     */

     JackPortCanMonitor = 0x8,

     /* JackPortIsTerminal means:

	for an input port: the data received by the port
	                   will not be passed on or made
			   available at any other port

        for an output port: the data available at the port
	                   does not originate from any 
			   other port

        Audio synthesizers, i/o h/w interface clients, HDR
        systems are examples of things that would set this
        flag for their ports.  
     */

     JackPortIsTerminal = 0x10
     
};	    

#define JACK_DEFAULT_AUDIO_TYPE "32 bit float mono audio"

jack_port_t *
jack_port_register (jack_client_t *,
		     const char *port_name,
		     const char *port_type,
		     unsigned long flags,
		     unsigned long buffer_size);

/* this removes the port from the client, disconnecting
   any existing connections at the same time.
*/

int jack_port_unregister (jack_client_t *, jack_port_t *);

/* a port is an opaque type. these help with a few things */

const char * jack_port_name (const jack_port_t *port);
const char * jack_port_short_name (const jack_port_t *port);
int          jack_port_flags (const jack_port_t *port);
const char * jack_port_type (const jack_port_t *port);

/* this returns TRUE or FALSE to indicate if there are
   any connections to/from the port argument.
*/

int jack_port_connected (const jack_port_t *port);

/* this returns TRUE or FALSE if the port argument is
   DIRECTLY connected to the port with the name given in
   `portname' 
*/

int jack_port_connected_to (const jack_port_t *port, const char *portname);
int jack_port_connected_to_port (const jack_port_t *port, const jack_port_t *other_port);

/* this returns a null-terminated array of port names to
   which the argument port is connected. if there are no
   connections, it returns NULL.

   the caller is responsible for calling free(3) on any
   non-NULL returned value.
*/   

const char ** jack_port_get_connections (const jack_port_t *port);

/* this modifies a port's name, and may be called at any
   time.
*/

int jack_port_set_name (jack_port_t *port, const char *name);

/* This returns a pointer to the memory area associated with the
   specified port. It can only be called from within the client's
   "process" callback. For an output port, it will be a memory area
   that can be written to; for an input port, it will be an area
   containing the data from the port's connection(s), or
   zero-filled. if there are multiple inbound connections, the data
   will be mixed appropriately.  

   You may not cache the values returned across process() callbacks.
   There is no guarantee that the values will not change from
   process() callback to process() callback.
*/

void *jack_port_get_buffer (jack_port_t *, nframes_t);

/* these two functions establish and disestablish a connection
   between two ports. when a connection exists, data written 
   to the source port will be available to be read at the destination
   port.

   the types of both ports must be identical to establish a connection.

   the flags of the source port must include PortIsOutput.
   the flags of the destination port must include PortIsInput.
*/

int jack_connect (jack_client_t *,
		  const char *source_port,
		  const char *destination_port);

int jack_disconnect (jack_client_t *,
		     const char *source_port,
		     const char *destination_port);

/* these two functions perform the exact same function
   as the jack_connect() and jack_disconnect(), but they
   use port handles rather than names, which avoids
   the name lookup inherent in the name-based versions.

   it is envisaged that clients (dis)connecting their own
   ports will use these two, wherease generic connection
   clients (e.g. patchbays) will use the name-based
   versions
*/

int jack_port_connect (jack_client_t *, jack_port_t *src, jack_port_t *dst);
int jack_port_disconnect (jack_client_t *, jack_port_t *);

/* A client may call this on a pair of its own ports to 
   semi-permanently wire them together. This means that
   a client that wants to direct-wire an input port to
   an output port can call this and then no longer
   have to worry about moving data between them. Any data
   arriving at the input port will appear automatically
   at the output port.

   The `destination' port must be an output port. The `source'
   port must be an input port. Both ports must belong to
   the same client. You cannot use this to tie ports between
   clients. Thats what a connection is for.
*/

int  jack_port_tie (jack_port_t *src, jack_port_t *dst);

/* This undoes the effect of jack_port_tie(). The port
   should be same as the `destination' port passed to
   jack_port_tie().
*/

int  jack_port_untie (jack_port_t *port);

/* a client may call this function to prevent other objects
   from changing the connection status of a port. the port
   must be owned by the calling client.
*/

int jack_port_lock (jack_client_t *, jack_port_t *);
int jack_port_unlock (jack_client_t *, jack_port_t *);

/* returns the time (in frames) between data being available
   or delivered at/to a port, and the time at which it
   arrived at or is delivered to the "other side" of the port.

   e.g. for a physical audio output port, this is the time between
   writing to the port and when the audio will be audible.

   for a physical audio input port, this is the time between the sound
   being audible and the corresponding frames being readable from the
   port.  
*/

nframes_t jack_port_get_latency (jack_port_t *port);

/* the port latency is zero by default. clients that control
   physical hardware with non-zero latency should call this
   to set the latency to its correct value. note that the value
   should include any systemic latency present "outside" the
   physical hardware controlled by the client. for example,
   for a client controlling a digital audio interface connected
   to an external digital converter, the latency setting should
   include both buffering by the audio interface *and* the converter. 
*/

void jack_port_set_latency (jack_port_t *, nframes_t);

/* if JackPortCanMonitor is set for a port, then these 2 functions will
   turn on/off input monitoring for the port. if JackPortCanMonitor
   is not set, then these functions will have no effect.
*/

int jack_port_request_monitor (jack_port_t *port, int onoff);
int jack_port_request_monitor_by_name (jack_client_t *client, const char *port_name, int onoff);

/* if JackPortCanMonitor is set for a port, then this function will
   turn on input monitoring if it was off, and will turn it off it
   only one request has been made to turn it on. if JackPortCanMonitor
   is not set, then this function will do nothing.
*/

int jack_port_ensure_monitor (jack_port_t *port, int onoff);

/* returns a true or false value depending on whether or not 
   input monitoring has been requested for `port'.
*/

int jack_port_monitoring_input (jack_port_t *port);

/* this returns the sample rate of the jack system */

unsigned long jack_get_sample_rate (jack_client_t *);

/* this returns the current maximum size that will
   ever be passed to the "process" callback. it should only
   be used *before* the client has been activated. after activation,
   the client will be notified of buffer size changes if it
   registers a buffer_size callback.
*/

nframes_t jack_get_buffer_size (jack_client_t *);

/* This function returns a NULL-terminated array of ports that match the
   specified arguments.
   
   port_name_pattern: a regular expression used to select ports by name.
                      if NULL or of zero length, no selection based on
		      name will be carried out.

   type_name_pattern: a regular expression used to select ports by type.
                      if NULL or of zero length, no selection based on
		      type will be carried out.

   flags:             a value used to select ports by their flags.  if 
                      zero, no selection based on flags will be carried out.

   The caller is responsible for calling free(3) any non-NULL returned
   value.
*/

const char ** jack_get_ports (jack_client_t *,
			       const char *port_name_pattern,
			       const char *type_name_pattern,
			       unsigned long flags);

jack_port_t *jack_port_by_name (jack_client_t *, const char *portname);

/* If a client is told (by the user) to become the timebase
   for the entire system, it calls this function. If it
   returns zero, then the client has the responsibility to
   call jack_update_time() at the end of its process()
   callback. Whatever time it provides (in frames since its
   reference zero time) becomes the current timebase for the
   entire system.  
*/

int  jack_engine_takeover_timebase (jack_client_t *);
void jack_update_time (jack_client_t *, nframes_t);

/* this estimates the time that has passed since the
   start jack server started calling the process()
   callbacks of all its clients.
*/

nframes_t jack_frames_since_cycle_start (jack_client_t *);

#ifdef __cplusplus
}
#endif

#endif /* __jack_h__ */

