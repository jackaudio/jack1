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

#include <pthread.h>

#include <jack/types.h>
#include <jack/error.h>
#include <jack/transport.h>

/**
 * Note: More documentation can be found in jack/types.h.
 */

/**
 * Attemps to become an external client of the Jack server.
 */
jack_client_t *jack_client_new (const char *client_name);

/**
 * Disconnects an external client from a JACK server.
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int jack_client_close (jack_client_t *client);

/**
 * @param client_name The name for the new client
 * @param so_name A path to a shared object file containing the code for the new client 
 * @param so_data An arbitary string containing information to be passed to the init() routine of the new client
 *
 * Attemps to load an internal client into the Jack server.
 */
int jack_internal_client_new (const char *client_name, const char *so_name, const char *so_data);

/**
 * Removes an internal client from a JACK server.
 *
 * @return 0 on success, otherwise a non-zero error code
 */
void jack_internal_client_close (const char *client_name);

/** 
 * @param client The Jack client structure.
 * @param function The jack_shutdown function pointer.
 * @param arg The arguments for the jack_shutdown function.
 *
 * Register a function (and argument) to be called if and when the
 * JACK server shuts down the client thread.  The function must
 * be written as if it were an asynchonrous POSIX signal
 * handler --- use only async-safe functions, and remember that it
 * is executed from another thread.  A typical function might
 * set a flag or write to a pipe so that the rest of the
 * application knows that the JACK client thread has shut
 * down.
 *
 * NOTE: clients do not need to call this.  It exists only
 * to help more complex clients understand what is going
 * on.  If called, it should be called before jack_client_activate().
 */
void jack_on_shutdown (jack_client_t *client, void (*function)(void *arg), void *arg);

/**
 * Tell the Jack server to call 'process_callback' whenever there is work
 * be done, passing 'arg' as the second argument.
 *
 * The code in the supplied function must be suitable for real-time
 * execution.  That means that it cannot call functions that might block for
 * a long time.  This includes malloc, free, printf, pthread_mutex_lock,
 * sleep, wait, poll, select, pthread_join, pthread_cond_wait, etc, etc. 
 * See
 * http://jackit.sourceforge.net/docs/design/design.html#SECTION00411000000000000000
 * for more information.
 * 
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int jack_set_process_callback (jack_client_t *, JackProcessCallback process_callback, void *arg);

/**
 * Tell the Jack server to call 'bufsize_callback' whenever the size of the
 * the buffer that will be passed to the process callback changes, 
 * passing 'arg' as the second argument.
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int jack_set_buffer_size_callback (jack_client_t *, JackBufferSizeCallback bufsize_callback, void *arg);

/**
 * Tell the Jack server to call 'srate_callback' whenever the sample rate of
 * the system changes.
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int jack_set_sample_rate_callback (jack_client_t *, JackSampleRateCallback srate_callback, void *arg);

/**
 * Tell the Jack server to call 'registration_callback' whenever a port is registered
 * or unregistered, passing 'arg' as a second argument.
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int jack_set_port_registration_callback (jack_client_t *, JackPortRegistrationCallback registration_callback, void *arg);

/**
 * Tell the Jack server to call 'registration_callback' whenever the processing
 * graph is reordered, passing 'arg' as an argument.
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int jack_set_graph_order_callback (jack_client_t *, JackGraphOrderCallback graph_callback, void *);

/**
 * Tell the Jack server to call 'xrun_callback' whenever there is a xrun, passing
 * 'arg' as an argument.
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int jack_set_xrun_callback (jack_client_t *, JackXRunCallback xrun_callback, void *arg);

/**
 * Tell the Jack server that the program is ready to start processing
 * audio.
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int jack_activate (jack_client_t *client);

/**
 * Tells the Jack server that the program should be removed from the 
 * processing graph. As a side effect, this will disconnect any
 * and all ports belonging to the client, since inactive clients
 * are not allowed to be connected to any other ports.
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int jack_deactivate (jack_client_t *client);

/**
 * This creates a new port for the client. 
 *
 * A port is an object used for moving data in or out of the client.
 * the data may be of any type. Ports may be connected to each other
 * in various ways.
 *
 * A port has a short name, which may be any non-NULL and non-zero
 * length string, and is passed as the first argument. A port's full
 * name is the name of the client concatenated with a colon (:) and
 * then its short name.
 *
 * A port has a type, which may be any non-NULL and non-zero length
 * string, and is passed as the second argument. For types that are
 * not built into the jack API (currently just
 * JACK_DEFAULT_AUDIO_TYPE) the client MUST supply a non-zero size
 * for the buffer as for 'buffer_size' . For builtin types, 
 * 'buffer_size' is ignored.
 *
 * The 'flags' argument is formed from a bitmask of JackPortFlags values.
 */
jack_port_t *jack_port_register (jack_client_t *,
                                 const char *port_name,
                                 const char *port_type,
                                 unsigned long flags,
                                 unsigned long buffer_size);

/** 
 * This removes the port from the client, disconnecting
 * any existing connections at the same time.
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int jack_port_unregister (jack_client_t *, jack_port_t *);

/**
 * This returns a pointer to the memory area associated with the
 * specified port. For an output port, it will be a memory area
 * that can be written to; for an input port, it will be an area
 * containing the data from the port's connection(s), or
 * zero-filled. if there are multiple inbound connections, the data
 * will be mixed appropriately.  
 *
 * You may cache the value returned, but only between calls to
 * your "blocksize" callback. For this reason alone, you should
 * either never cache the return value or ensure you have
 * a "blocksize" callback and be sure to invalidate the cached
 * address from there.
 */
void *jack_port_get_buffer (jack_port_t *, jack_nframes_t);

/**
 * Returns the name of the jack_port_t.
 */
const char * jack_port_name (const jack_port_t *port);

/**
 * Returns the short name of the jack_port_t.
 */
const char * jack_port_short_name (const jack_port_t *port);

/**
 * Returns the flags of the jack_port_t.
 */
int jack_port_flags (const jack_port_t *port);

/**
 * Returns the type of the jack_port_t.
 */
const char * jack_port_type (const jack_port_t *port);

/** 
 * Returns 1 if the jack_port_t belongs to the jack_client_t.
 */
int jack_port_is_mine (const jack_client_t *, const jack_port_t *port);

/** 
 * This returns a positive integer indicating the number
 * of connections to or from 'port'. 
 *
 * ®pre The calling client must own 'port'.
 */
int jack_port_connected (const jack_port_t *port);

/**
 * This returns TRUE or FALSE if the port argument is
 * DIRECTLY connected to the port with the name given in 'portname' 
 *
 * @pre The calling client must own 'port'.
 */
int jack_port_connected_to (const jack_port_t *port, const char *portname);

/**
 * This returns a null-terminated array of port names to which 
 * the argument port is connected. if there are no connections, it 
 * returns NULL.
 *
 * The caller is responsible for calling free(3) on any
 * non-NULL returned value.
 *
 * @pre The calling client must own 'port'.
 *
 * See jack_port_get_all_connections() for an alternative.
 */   
const char ** jack_port_get_connections (const jack_port_t *port);

/**
 * This returns a null-terminated array of port names to which 
 * the argument port is connected. if there are no connections, it 
 * returns NULL.
 *
 * The caller is responsible for calling free(3) on any
 * non-NULL returned value.
 *
 * It differs from jack_port_get_connections() in two important
 * respects:
 *
 *     1) You may not call this function from code that is
 *          executed in response to a JACK event. For example,
 *          you cannot use it in a GraphReordered handler.
 *
 *     2) You need not be the owner of the port to get information
 *          about its connections. 
 */   
const char ** jack_port_get_all_connections (const jack_client_t *client, const jack_port_t *port);

/**
 * A client may call this on a pair of its own ports to 
 * semi-permanently wire them together. This means that
 * a client that wants to direct-wire an input port to
 * an output port can call this and then no longer
 * have to worry about moving data between them. Any data
 * arriving at the input port will appear automatically
 * at the output port.
 *
 * The 'destination' port must be an output port. The 'source'
 * port must be an input port. Both ports must belong to
 * the same client. You cannot use this to tie ports between
 * clients. That is what a connection is for.
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int  jack_port_tie (jack_port_t *src, jack_port_t *dst);

/**
 * This undoes the effect of jack_port_tie(). The port
 * should be same as the 'destination' port passed to
 * jack_port_tie().
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int  jack_port_untie (jack_port_t *port);

/**
 * A client may call this function to prevent other objects
 * from changing the connection status of a port. The port
 * must be owned by the calling client.
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int jack_port_lock (jack_client_t *, jack_port_t *);

/**
 * This allows other objects to change the connection status of a port.
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int jack_port_unlock (jack_client_t *, jack_port_t *);

/**
 * Returns the time (in frames) between data being available
 * or delivered at/to a port, and the time at which it
 * arrived at or is delivered to the "other side" of the port.
 * E.g. for a physical audio output port, this is the time between
 * writing to the port and when the audio will be audible.
 * For a physical audio input port, this is the time between the sound
 * being audible and the corresponding frames being readable from the
 * port.  
 */
jack_nframes_t jack_port_get_latency (jack_port_t *port);

/**
 * The maximum of the sum of the latencies in every
 * connection path that can be drawn between the port and other
 * ports with the JackPortIsTerminal flag set.
 */
jack_nframes_t jack_port_get_total_latency (jack_client_t *, jack_port_t *port);

/**
 * The port latency is zero by default. Clients that control
 * physical hardware with non-zero latency should call this
 * to set the latency to its correct value. Note that the value
 * should include any systemic latency present "outside" the
 * physical hardware controlled by the client. For example,
 * for a client controlling a digital audio interface connected
 * to an external digital converter, the latency setting should
 * include both buffering by the audio interface *and* the converter. 
 */
void jack_port_set_latency (jack_port_t *, jack_nframes_t);

/**
 * This modifies a port's name, and may be called at any time.
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int jack_port_set_name (jack_port_t *port, const char *name);

/**
 */

double jack_port_get_peak (jack_port_t*, jack_nframes_t);

/**
 */

double jack_port_get_power (jack_port_t*, jack_nframes_t);

/**
 */

void jack_port_set_peak_function (jack_port_t *, double (*func)(jack_port_t*, jack_nframes_t));

/**
 */

void jack_port_set_power_function (jack_port_t *, double (*func)(jack_port_t*, jack_nframes_t));

/**
 * If JackPortCanMonitor is set for a port, then these 2 functions will
 * turn on/off input monitoring for the port. If JackPortCanMonitor
 * is not set, then these functions will have no effect.
 */
int jack_port_request_monitor (jack_port_t *port, int onoff);

/**
 * If JackPortCanMonitor is set for a port, then these 2 functions will
 * turn on/off input monitoring for the port. If JackPortCanMonitor
 * is not set, then these functions will have no effect.
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int jack_port_request_monitor_by_name (jack_client_t *client, const char *port_name, int onoff);

/**
 * If JackPortCanMonitor is set for a port, then this function will
 * turn on input monitoring if it was off, and will turn it off it
 * only one request has been made to turn it on.  If JackPortCanMonitor
 * is not set, then this function will do nothing.
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int jack_port_ensure_monitor (jack_port_t *port, int onoff);

/**
 * Returns a true or false value depending on whether or not 
 * input monitoring has been requested for 'port'.
 */
int jack_port_monitoring_input (jack_port_t *port);

/**
 * Establishes a connection between two ports.
 *
 * When a connection exists, data written to the source port will
 * be available to be read at the destination port.
 *
 * @pre The types of both ports must be identical to establish a connection.
 * @pre The flags of the source port must include PortIsOutput.
 * @pre The flags of the destination port must include PortIsInput.
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int jack_connect (jack_client_t *,
		  const char *source_port,
		  const char *destination_port);

/**
 * Removes a connection between two ports.
 *
 * @pre The types of both ports must be identical to establish a connection.
 * @pre The flags of the source port must include PortIsOutput.
 * @pre The flags of the destination port must include PortIsInput.
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int jack_disconnect (jack_client_t *,
		     const char *source_port,
		     const char *destination_port);

/**
 * Performs the exact same function as jack_connect(), but it uses
 * port handles rather than names, which avoids the name lookup inherent
 * in the name-based version.
 *
 * It is envisaged that clients connecting their own ports will use these
 * two, whereas generic connection clients (e.g. patchbays) will use the
 * name-based versions.
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int jack_port_connect (jack_client_t *, jack_port_t *src, jack_port_t *dst);

/**
 * Performs the exact same function as jack_disconnect(), but it uses
 * port handles rather than names, which avoids the name lookup inherent
 * in the name-based version.
 *
 * It is envisaged that clients disconnecting their own ports will use these
 * two, whereas generic connection clients (e.g. patchbays) will use the
 * name-based versions.
 */
int jack_port_disconnect (jack_client_t *, jack_port_t *);

/**
 * This returns the sample rate of the jack system, as set by the user when
 * jackd was started.
 */
unsigned long jack_get_sample_rate (jack_client_t *);

/**
 * This returns the current maximum size that will
 * ever be passed to the "process" callback.  It should only
 * be used *before* the client has been activated.  After activation,
 * the client will be notified of buffer size changes if it
 * registers a buffer_size callback.
 */
jack_nframes_t jack_get_buffer_size (jack_client_t *);

/**
 * @param port_name_pattern A regular expression used to select 
 * ports by name.  If NULL or of zero length, no selection based 
 * on name will be carried out.
 * @param type_name_pattern A regular expression used to select 
 * ports by type.  If NULL or of zero length, no selection based 
 * on type will be carried out.
 * @param flags A value used to select ports by their flags.  
 * If zero, no selection based on flags will be carried out.
 *
 * This function returns a NULL-terminated array of ports that match 
 * the specified arguments.
 * The caller is responsible for calling free(3) any non-NULL returned value.
 */
const char ** jack_get_ports (jack_client_t *, 
			      const char *port_name_pattern, 
			      const char *type_name_pattern, 
			      unsigned long flags);

/**
 * Searchs for and returns the jack_port_t with the name value
 * from portname.
 */
jack_port_t *jack_port_by_name (jack_client_t *, const char *portname);

/**
 * Searchs for and returns the jack_port_t of id 'id'.
 */
jack_port_t *jack_port_by_id (const jack_client_t *client, jack_port_id_t id);

/**
 * If a client is told (by the user) to become the timebase
 * for the entire system, it calls this function. If it
 * returns zero, then the client has the responsibility to
 * call jack_set_transport_info()) at the end of its process()
 * callback. 
 *
 * @return 0 on success, otherwise a non-zero error code
 */
int  jack_engine_takeover_timebase (jack_client_t *);

/**
 * undocumented
 */
void jack_update_time (jack_client_t *, jack_nframes_t);

/**
 * This estimates the time that has passed since the
 * start jack server started calling the process()
 * callbacks of all its clients.
 */
jack_nframes_t jack_frames_since_cycle_start (const jack_client_t *);

/**
 * Return an estimate of the current time in frames. It is a running
 * counter - no significance should be attached to the return
 * value. it should be used to compute the difference between
 * a previously returned value.
 */
jack_nframes_t jack_frame_time (const jack_client_t *);

/**
 * This returns the current CPU load estimated by JACK
 * as a percentage. The load is computed by measuring
 * the amount of time it took to execute all clients
 * as a fraction of the total amount of time
 * represented by the data that was processed.
 */
float jack_cpu_load (jack_client_t *client);

/**
 * Set the directory in which the server is expected
 * to have put its communication FIFOs. A client
 * will need to call this before calling
 * jack_client_new() if the server was started
 * with arguments telling it to use a non-standard
 * directory.
 */
void jack_set_server_dir (const char *path);

/**
 * Return the pthread ID of the thread running the JACK client
 * side code.
 */
pthread_t jack_client_thread_id (jack_client_t *);

#ifdef __cplusplus
}
#endif

#endif /* __jack_h__ */


