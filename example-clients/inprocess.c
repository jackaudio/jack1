#include <stdio.h>
#include <memory.h>
#include <jack/jack.h>

jack_port_t *input_port;
jack_port_t *output_port;

static int
process (jack_nframes_t nframes, void *arg)
{
	jack_default_audio_sample_t *out = (jack_default_audio_sample_t *) jack_port_get_buffer (output_port, nframes);
	jack_default_audio_sample_t *in = (jack_default_audio_sample_t *) jack_port_get_buffer (input_port, nframes);

	memcpy (out, in, sizeof (jack_default_audio_sample_t) * nframes);

	return 0;      
}

int
jack_initialize (jack_client_t *client, const char *data)
{
	jack_set_process_callback (client, process, 0);

	/* create two ports */

	input_port = jack_port_register (client, "input", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
	output_port = jack_port_register (client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

	/* start the client */

	jack_activate (client);

	/* connect the ports */

	if (jack_connect (client, "alsa_pcm:capture_1", jack_port_name (input_port))) {
		fprintf (stderr, "cannot connect input port\n");
	}

	if (jack_connect (client, jack_port_name (output_port), "alsa_pcm:playback_1")) {
		fprintf (stderr, "cannot connect output port\n");
	}

	/* our client is running. we're happy */
	
	return 0;
}

void
jack_finish (void)
{
	/* relax */
}
