/** @file inprocess.c
 *
 * @brief This demonstrates the basic concepts for writing a client
 * that runs within the JACK server process.
 *
 * For the sake of example, a port_pair_t is allocated in
 * jack_initialize(), passed to process() as an argument, then freed
 * in jack_finish().
 */

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <jack/jack.h>

/**
 * For the sake of example, an instance of this struct is allocated in
 * jack_initialize(), passed to process() as an argument, then freed
 * in jack_finish().
 */
typedef struct {
	jack_port_t *input_port;
	jack_port_t *output_port;
} port_pair_t;

/**
 * Called in the realtime thread on every process cycle.  The entry
 * point name was passed to jack_set_process_callback() by
 * jack_initialize().
 *
 * @return 0 if successful; otherwise jack_finish() will be called and
 * the client terminated immediately.
 */
int
process (jack_nframes_t nframes, void *arg)
{
	port_pair_t *pp = arg;
	jack_default_audio_sample_t *out =
		jack_port_get_buffer (pp->output_port, nframes);
	jack_default_audio_sample_t *in =
		jack_port_get_buffer (pp->input_port, nframes);

	memcpy (out, in, sizeof (jack_default_audio_sample_t) * nframes);

	return 0;			/* continue */
}

/**
 * This required entry point is called after the client is loaded by
 * jack_internal_client_new().
 *
 * @param client pointer to JACK client structure.
 * @param so_data character string passed from jack_internal_client_new().
 *
 * @return 0 if successful; otherwise jack_finish() will be called and
 * the client terminated immediately.
 */
int
jack_initialize (jack_client_t *client, const char *so_data)
{
	port_pair_t *pp = malloc (sizeof (port_pair_t));

	if (pp == NULL)
		return 1;		/* heap exhausted */

	jack_set_process_callback (client, process, pp);

	/* create a pair of ports */
	pp->input_port = jack_port_register (client, "input",
					     JACK_DEFAULT_AUDIO_TYPE,
					     JackPortIsInput, 0);
	pp->output_port = jack_port_register (client, "output",
					      JACK_DEFAULT_AUDIO_TYPE,
					      JackPortIsOutput, 0);

	/* join the process() cycle */
	jack_activate (client);

	/* connect the ports */
	if (jack_connect (client, "alsa_pcm:capture_1",
			  jack_port_name (pp->input_port))) {
		fprintf (stderr, "cannot connect input port\n");
		return 1;		/* terminate client */
	}

	if (jack_connect (client, jack_port_name (pp->output_port),
			  "alsa_pcm:playback_1")) {
		fprintf (stderr, "cannot connect output port\n");
		return 1;		/* terminate client */
	}

	return 0;			/* success */
}

/**
 * This required entry point is called immediately before the client
 * is unloaded, which could happen due to a call to
 * jack_internal_client_close(), or a nonzero return from either
 * jack_initialize() or process().
 *
 * @param arg the same parameter provided to process().
 */
void
jack_finish (void *arg)
{
	if (arg)
		free ((port_pair_t *) arg);
}
