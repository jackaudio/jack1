/** @file simple_client.c
 *
 * @brief This simple client demonstrates the basic features of JACK
 * as they would be used by many applications.
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <alloca.h>

#include <jack/jack.h>

#define ALSA_PCM_OLD_HW_PARAMS_API
#define ALSA_PCM_OLD_SW_PARAMS_API
#include "alsa/asoundlib.h"

#include <samplerate.h>

typedef signed short OUTPUTSAMPLE;

#define SAMPLE_RATE 48000
#define PRIVATE static
#define SAMPLE jack_default_audio_sample_t

jack_port_t *input_port;
jack_port_t *output_port1, *output_port2;
jack_client_t *client;

snd_pcm_format_t format = SND_PCM_FORMAT_S16;	 /* sample format */
int rate = SAMPLE_RATE;				 /* stream rate */
int channels = 2;				 /* count of channels */
int buffer_time = 1000000*256 / SAMPLE_RATE;	 /* ring buffer length in us */
int period_time = 1000000*128 / SAMPLE_RATE;	 /* period time in us */

int target_delay = 150;	    /* the delay which the program should try to approach. */
int max_diff = 32;	    /* the diff value, when a hard readpointer skip should occur */

snd_pcm_sframes_t buffer_size;
snd_pcm_sframes_t period_size;
snd_output_t *output = NULL;

snd_pcm_t *alsa_handle;

SRC_STATE *src_state;

static int xrun_recovery(snd_pcm_t *handle, int err)
{
    //printf( "xrun !!!....\n" );
    if (err == -EPIPE) {	/* under-run */
        err = snd_pcm_prepare(handle);
        if (err < 0)
            printf("Can't recovery from underrun, prepare failed: %s\n", snd_strerror(err));
        return 0;
    } else if (err == -ESTRPIPE) {
        while ((err = snd_pcm_resume(handle)) == -EAGAIN)
            sleep(1);	/* wait until the suspend flag is released */
        if (err < 0) {
            err = snd_pcm_prepare(handle);
            if (err < 0)
                printf("Can't recovery from suspend, prepare failed: %s\n", snd_strerror(err));
        }
        return 0;
    }
    return err;
}

PRIVATE void audio_input_fragment(snd_pcm_t *handle, SAMPLE *left, SAMPLE *right, int length)
{
    OUTPUTSAMPLE *outbuf;
    float *floatbuf, *resampbuf;
    int rlen;
    int i, err;
    snd_pcm_sframes_t delay;
    SRC_DATA src_data;

    if (length <= 0)
        return;

    snd_pcm_delay( handle, &delay );

    // Do it the hard way.
    // this is for compensating xruns etc...

    if ( delay > (target_delay + max_diff) ) {
        OUTPUTSAMPLE *tmp = alloca( (delay - target_delay) * sizeof( OUTPUTSAMPLE ) * 2 );
        snd_pcm_readi( handle, tmp, delay - target_delay );
        printf( "delay = %d\n", (int) delay );
        delay = target_delay;
    }
    if ( delay < (target_delay - max_diff) ) {
        snd_pcm_rewind( handle, target_delay - delay );
        printf( "delay = %d\n", (int) delay );
        delay = target_delay;
    }

    // ok... now to the resampling code...
    //

    rlen = length - target_delay + delay; //(target_delay/10) + (delay/10);


    outbuf = alloca( rlen * sizeof( OUTPUTSAMPLE ) * 2 );
    floatbuf = alloca( rlen * sizeof( float ) * 2 );
    resampbuf = alloca( length * sizeof( float ) * 2 );

again:
    err = snd_pcm_readi(handle, outbuf, rlen);
    if ( err < 0 ) {
        //printf( "err = %d\n", err );
        if (xrun_recovery(handle, err) < 0) {
            //printf("Write error: %s\n", snd_strerror(err));
            //exit(EXIT_FAILURE);
        }
        goto again;
    }
    if ( err != rlen ) {
        printf( "read = %d\n", rlen );
    }

    for (i = 0; i < (rlen*2); i++) {
        floatbuf[i] = (float)outbuf[i] / (float)32767;
    }

    src_data.data_in = floatbuf;
    src_data.data_out = resampbuf;
    src_data.input_frames = rlen;
    src_data.output_frames = length;
    src_data.src_ratio = (double)length / (double)rlen;
    src_data.end_of_input = 0;

    src_set_ratio( src_state, src_data.src_ratio );
    src_process( src_state, &src_data );

//  for( i=0; i < length*2; i++ )
//      resampbuf[i] = floatbuf[i];

    for (i = 0; i < length; i++) {
        left[i] = resampbuf[i*2];
        right[i] = resampbuf[(i*2)+1];
    }
    //printf( "len=%d, err=%d state=%d\n", length, err, snd_pcm_state(handle) );
}

static int set_hwparams(snd_pcm_t *handle,
                        snd_pcm_hw_params_t *params,
                        snd_pcm_access_t access)
{
    int err, dir;

    /* choose all parameters */
    err = snd_pcm_hw_params_any(handle, params);
    if (err < 0) {
        printf("Broken configuration for playback: no configurations available: %s\n", snd_strerror(err));
        return err;
    }
    /* set the interleaved read/write format */
    err = snd_pcm_hw_params_set_access(handle, params, access);
    if (err < 0) {
        printf("Access type not available for playback: %s\n", snd_strerror(err));
        return err;
    }
    /* set the sample format */
    err = snd_pcm_hw_params_set_format(handle, params, format);
    if (err < 0) {
        printf("Sample format not available for playback: %s\n", snd_strerror(err));
        return err;
    }
    /* set the count of channels */
    err = snd_pcm_hw_params_set_channels(handle, params, channels);
    if (err < 0) {
        printf("Channels count (%i) not available for record: %s\n", channels, snd_strerror(err));
        return err;
    }
    /* set the stream rate */
    err = snd_pcm_hw_params_set_rate_near(handle, params, rate, 0);
    if (err < 0) {
        printf("Rate %iHz not available for capture: %s\n", rate, snd_strerror(err));
        return err;
    }
    if (err != rate) {
        printf("Rate doesn't match (requested %iHz, get %iHz)\n", rate, err);
        return -EINVAL;
    }
    /* set the buffer time */
    err = snd_pcm_hw_params_set_buffer_time_near(handle, params, buffer_time, &dir);
    if (err < 0) {
        printf("Unable to set buffer time %i for playback: %s\n", buffer_time, snd_strerror(err));
        return err;
    }
    buffer_size = snd_pcm_hw_params_get_buffer_size(params);
    /* set the period time */
    err = snd_pcm_hw_params_set_period_time_near(handle, params, period_time, &dir);
    if (err < 0) {
        printf("Unable to set period time %i for playback: %s\n", period_time, snd_strerror(err));
        return err;
    }
    period_size = snd_pcm_hw_params_get_period_size(params, &dir);
    /* write the parameters to device */
    err = snd_pcm_hw_params(handle, params);
    if (err < 0) {
        printf("Unable to set hw params for playback: %s\n", snd_strerror(err));
        return err;
    }
    printf( "bs=%d, ps=%d\n", (int)buffer_size, (int)period_size );
    return 0;
}

static int set_swparams(snd_pcm_t *handle, snd_pcm_sw_params_t *swparams)
{
    int err;

    /* get the current swparams */
    err = snd_pcm_sw_params_current(handle, swparams);
    if (err < 0) {
        printf("Unable to determine current swparams for capture: %s\n", snd_strerror(err));
        return err;
    }
    /* start the transfer when the buffer is full */
    err = snd_pcm_sw_params_set_start_threshold(handle, swparams, buffer_size );
    if (err < 0) {
        printf("Unable to set start threshold mode for capture: %s\n", snd_strerror(err));
        return err;
    }
    err = snd_pcm_sw_params_set_stop_threshold(handle, swparams, -1 );
    if (err < 0) {
        printf("Unable to set start threshold mode for capture: %s\n", snd_strerror(err));
        return err;
    }
    /* allow the transfer when at least period_size samples can be processed */
    err = snd_pcm_sw_params_set_avail_min(handle, swparams, period_size );
    if (err < 0) {
        printf("Unable to set avail min for capture: %s\n", snd_strerror(err));
        return err;
    }
    /* align all transfers to 1 sample */
    err = snd_pcm_sw_params_set_xfer_align(handle, swparams, 1);
    if (err < 0) {
        printf("Unable to set transfer align for capture: %s\n", snd_strerror(err));
        return err;
    }
    /* write the parameters to the playback device */
    err = snd_pcm_sw_params(handle, swparams);
    if (err < 0) {
        printf("Unable to set sw params for capture: %s\n", snd_strerror(err));
        return err;
    }
    return 0;
}


PRIVATE snd_pcm_t *open_audiofd( void )
{
    int err;
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *hwparams;
    snd_pcm_sw_params_t *swparams;

    snd_pcm_hw_params_alloca(&hwparams);
    snd_pcm_sw_params_alloca(&swparams);

    if ((err = snd_pcm_open(&(handle), "hw:0", SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK )) < 0) {
        printf("Capture open error: %s\n", snd_strerror(err));
        return NULL;
    }

    if ((err = set_hwparams(handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED )) < 0) {
        printf("Setting of hwparams failed: %s\n", snd_strerror(err));
        return NULL;
    }
    if ((err = set_swparams(handle, swparams)) < 0) {
        printf("Setting of swparams failed: %s\n", snd_strerror(err));
        return NULL;
    }

    snd_pcm_start( handle );
    snd_pcm_wait( handle, 200 );

    return handle;
}
/**
 * The process callback for this JACK application.
 * It is called by JACK at the appropriate times.
 */
int
process (jack_nframes_t nframes, void *arg)
{
    jack_default_audio_sample_t *out1 = (jack_default_audio_sample_t *) jack_port_get_buffer (output_port1, nframes);
    jack_default_audio_sample_t *out2 = (jack_default_audio_sample_t *) jack_port_get_buffer (output_port2, nframes);
    //jack_default_audio_sample_t *in = (jack_default_audio_sample_t *) jack_port_get_buffer (input_port, nframes);

    //memcpy (out, in, sizeof (jack_default_audio_sample_t) * nframes);

    audio_input_fragment( alsa_handle, out1, out2, nframes );

    //sendto( sockfd, "x", 1, 0, &destaddr, sizeof( destaddr ) );
    return 0;
}

/**
 * This is the shutdown callback for this JACK application.
 * It is called by JACK if the server ever shuts down or
 * decides to disconnect the client.
 */
void
jack_shutdown (void *arg)
{

    exit (1);
}


int
main (int argc, char *argv[])
{
    //const char **ports;


//	if (argc < 2) {
//		fprintf (stderr, "usage: udpsync_source desthost\n");
//		return 1;
//	}

//	sockfd = socket( PF_INET, SOCK_DGRAM, 0 );
//	init_sockaddr_in( &destaddr, argv[1], 3000 );
    /* try to become a client of the JACK server */


    src_state = src_new(SRC_SINC_FASTEST, 2, NULL);
    src_set_ratio( src_state, 1.0 );

    alsa_handle = open_audiofd();

    if ((client = jack_client_new ("alsa_unsynced_pcm")) == 0) {
        fprintf (stderr, "jack server not running?\n");
        return 1;
    }

    /* tell the JACK server to call `process()' whenever
       there is work to be done.
    */

    jack_set_process_callback(client, process, 0);

    /* tell the JACK server to call `jack_shutdown()' if
       it ever shuts down, either entirely, or if it
       just decides to stop calling us.
    */

    jack_on_shutdown(client, jack_shutdown, 0);

    /* display the current sample rate.
     */

    printf("engine sample rate: %" PRIu32 "\n",
            jack_get_sample_rate (client));

    /* create two ports */

//	input_port = jack_port_register (client, "input", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    output_port1 = jack_port_register(client, "output1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    output_port2 = jack_port_register(client, "output2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

    /* tell the JACK server that we are ready to roll */

    if (jack_activate(client)) {
        fprintf (stderr, "cannot activate client");
        return 1;
    }

    while (1) sleep(1);
    jack_client_close(client);
    exit (0);
}

