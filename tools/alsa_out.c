/** @file simple_client.c
 *
 * @brief This simple client demonstrates the basic features of JACK
 * as they would be used by many applications.
 */

#define _ISOC99_SOURCE  1
#define _XOPEN_SOURCE   600

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <alloca.h>
#include <math.h>

#include <jack/jack.h>
#include <jack/jslist.h>

#include "alsa/asoundlib.h"

#include <samplerate.h>

#define SAMPLE_16BIT_SCALING  32767.0f
#define SAMPLE_16BIT_MAX  32767
#define SAMPLE_16BIT_MIN  -32767
#define NORMALIZED_FLOAT_MIN -1.0f
#define NORMALIZED_FLOAT_MAX  1.0f
#define f_round(f) lrintf(f)

#define float_16(s, d)\
	if ((s) <= NORMALIZED_FLOAT_MIN) {\
		(d) = SAMPLE_16BIT_MIN;\
	} else if ((s) >= NORMALIZED_FLOAT_MAX) {\
		(d) = SAMPLE_16BIT_MAX;\
	} else {\
		(d) = f_round ((s) * SAMPLE_16BIT_SCALING);\
	}

typedef signed short ALSASAMPLE;

// Here are the lists of the jack ports...

JSList	   *capture_ports = NULL;
JSList	   *capture_srcs = NULL;
JSList	   *playback_ports = NULL;
JSList	   *playback_srcs = NULL;
jack_client_t *client;

// TODO: make the sample format configurable soon...
snd_pcm_format_t format = SND_PCM_FORMAT_S16;	 /* sample format */

snd_pcm_t *alsa_handle;

int jack_sample_rate;

double current_resample_factor = 1.0;

// ------------------------------------------------------ commandline parameters

int sample_rate = 0;				 /* stream rate */
int num_channels = 2;				 /* count of channels */
int period_size = 1024;
int num_periods = 2;

int target_delay = 0;	    /* the delay which the program should try to approach. */
int max_diff = 0;	    /* the diff value, when a hard readpointer skip should occur */
int catch_factor = 1000;
int catch_factor2 = 1000000;
int good_window=0;

// Debug stuff:

int print_counter = 10;

volatile float output_resampling_factor = 0.0;
volatile int output_new_delay = 0;
volatile float output_offset = 0.0;
volatile float output_diff = 0.0;

snd_pcm_uframes_t real_buffer_size;
snd_pcm_uframes_t real_period_size;

// Alsa stuff... i dont want to touch this bullshit in the next years.... please...

static int xrun_recovery(snd_pcm_t *handle, int err) {
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

static int set_hwparams(snd_pcm_t *handle, snd_pcm_hw_params_t *params, snd_pcm_access_t access, int rate, int channels, int period, int nperiods ) {
	int err, dir=0;
	unsigned int buffer_time;
	unsigned int period_time;
	unsigned int rrate;

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
	rrate = rate;
	err = snd_pcm_hw_params_set_rate_near(handle, params, &rrate, 0);
	if (err < 0) {
		printf("Rate %iHz not available for playback: %s\n", rate, snd_strerror(err));
		return err;
	}
	if (rrate != rate) {
		printf("Rate doesn't match (requested %iHz, get %iHz)\n", rate, rrate);
		return -EINVAL;
	}
	/* set the buffer time */

	buffer_time = 1000000*period*nperiods/rate;
	err = snd_pcm_hw_params_set_buffer_time_near(handle, params, &buffer_time, &dir);
	if (err < 0) {
		printf("Unable to set buffer time %i for playback: %s\n",  1000000*period*nperiods/rate, snd_strerror(err));
		return err;
	}
	err = snd_pcm_hw_params_get_buffer_size( params, &real_buffer_size );
	if (err < 0) {
		printf("Unable to get buffer size back: %s\n", snd_strerror(err));
		return err;
	}
	if( real_buffer_size != nperiods * period ) {
	    printf( "WARNING: buffer size does not match: (requested %d, got %d)\n", nperiods * period, (int) real_buffer_size );
	}
	/* set the period time */
	period_time = 1000000*period/rate;
	err = snd_pcm_hw_params_set_period_time_near(handle, params, &period_time, &dir);
	if (err < 0) {
		printf("Unable to set period time %i for playback: %s\n", 1000000*period/rate, snd_strerror(err));
		return err;
	}
	err = snd_pcm_hw_params_get_period_size(params, &real_period_size, NULL );
	if (err < 0) {
		printf("Unable to get period size back: %s\n", snd_strerror(err));
		return err;
	}
	if( real_period_size != period ) {
	    printf( "WARNING: period size does not match: (requested %i, got %i)\n", period, (int)real_period_size );
	}
	/* write the parameters to device */
	err = snd_pcm_hw_params(handle, params);
	if (err < 0) {
		printf("Unable to set hw params for playback: %s\n", snd_strerror(err));
		return err;
	}
	return 0;
}

static int set_swparams(snd_pcm_t *handle, snd_pcm_sw_params_t *swparams, int period, int nperiods) {
	int err;

	/* get the current swparams */
	err = snd_pcm_sw_params_current(handle, swparams);
	if (err < 0) {
		printf("Unable to determine current swparams for capture: %s\n", snd_strerror(err));
		return err;
	}
	/* start the transfer when the buffer is full */
	err = snd_pcm_sw_params_set_start_threshold(handle, swparams, period );
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
	err = snd_pcm_sw_params_set_avail_min(handle, swparams, 1 );
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

// ok... i only need this function to communicate with the alsa bloat api...

static snd_pcm_t *open_audiofd( char *device_name, int capture, int rate, int channels, int period, int nperiods ) {
  int err;
  snd_pcm_t *handle;
  snd_pcm_hw_params_t *hwparams;
  snd_pcm_sw_params_t *swparams;

  snd_pcm_hw_params_alloca(&hwparams);
  snd_pcm_sw_params_alloca(&swparams);

  if ((err = snd_pcm_open(&(handle), device_name, capture ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK )) < 0) {
      printf("Capture open error: %s\n", snd_strerror(err));
      return NULL;
  }

  if ((err = set_hwparams(handle, hwparams,SND_PCM_ACCESS_RW_INTERLEAVED, rate, channels, period, nperiods )) < 0) {
      printf("Setting of hwparams failed: %s\n", snd_strerror(err));
      return NULL;
  }
  if ((err = set_swparams(handle, swparams, period, nperiods)) < 0) {
      printf("Setting of swparams failed: %s\n", snd_strerror(err));
      return NULL;
  }

  //snd_pcm_start( handle );
  //snd_pcm_wait( handle, 200 );
  int num_null_samples = nperiods * period * channels;
  ALSASAMPLE *tmp = alloca( num_null_samples * sizeof( ALSASAMPLE ) ); 
  memset( tmp, 0, num_null_samples * sizeof( ALSASAMPLE ) );
  snd_pcm_writei( handle, tmp, num_null_samples );
  

  return handle;
}

/**
 * The process callback for this JACK application.
 * It is called by JACK at the appropriate times.
 */
int process (jack_nframes_t nframes, void *arg) {

    ALSASAMPLE *outbuf;
    float *floatbuf, *resampbuf;
    int rlen;
    int err;
    snd_pcm_sframes_t delay;

    double offset;
    double diff_value;

    snd_pcm_delay( alsa_handle, &delay );


    // Do it the hard way.
    // this is for compensating xruns etc...

    if( delay > (target_delay+max_diff) ) {
	snd_pcm_rewind( alsa_handle, delay - target_delay );
	output_new_delay = (int) delay;

	delay = target_delay;
	current_resample_factor = (double) sample_rate / (double) jack_sample_rate;
    }
    if( delay < (target_delay-max_diff) ) {
	ALSASAMPLE *tmp = alloca( (target_delay-delay) * sizeof( ALSASAMPLE ) * num_channels ); 
	memset( tmp, 0, sizeof( ALSASAMPLE ) * num_channels * (target_delay-delay) );
	snd_pcm_writei( alsa_handle, tmp, target_delay-delay );

	output_new_delay = (int) delay;

	delay = target_delay;
	current_resample_factor = (double) sample_rate / (double) jack_sample_rate;
    }
    /* ok... now we should have target_delay +- max_diff on the alsa side.
     *
     * calculate the number of frames, we want to get.
     */

    double request_samples = nframes * current_resample_factor;  //== alsa_samples;

    offset = delay - target_delay;

    double frlen = request_samples - offset;

    // Calculate the added resampling factor, which would move us straight to target delay.
    double compute_factor = frlen / (double) nframes;

    // Now calculate the diff_value, which we want to add to current_resample_factor
    // here are the coefficients of the dll.
    diff_value =  pow(current_resample_factor - compute_factor, 3) / (double) catch_factor;
    diff_value +=  pow(current_resample_factor - compute_factor, 1) / (double) catch_factor2;
    current_resample_factor -= diff_value;

    // Dampening:
    // use hysteresis, only do it once offset was more than 150 off,
    // and now came into 50samples window.
    // Also only damp when current_resample_factor is more than 0.01% off.
    if( good_window ) { 
	    if( (offset > 150) || (offset < -150) ) {
		    good_window = 0;
	    }
    } else {
	    if( (offset < 50) && (offset > -50) ) {
		    if( 0.0001 < fabs( current_resample_factor - ((double) sample_rate / (double) jack_sample_rate) ) )
			    current_resample_factor = ((double) sample_rate / (double) jack_sample_rate);
		    good_window = 1;
	    }
    }

    // Output "instrumentatio" gonna change that to real instrumentation in a few.
    output_resampling_factor = (float) current_resample_factor;
    output_diff = (float) diff_value;
    output_offset = (float) offset;

    // Clamp a bit.
    if( current_resample_factor < 0.25 ) current_resample_factor = 0.25;
    if( current_resample_factor > 4 ) current_resample_factor = 4;
    rlen = ceil( ((double)nframes) * current_resample_factor )+2;
    assert( rlen > 10 );

    /*
     * now this should do it...
     */

    outbuf = alloca( rlen * sizeof( ALSASAMPLE ) * num_channels );

    floatbuf = alloca( rlen * sizeof( float ) );
    resampbuf = alloca( nframes * sizeof( float ) );
    /*
     * render jack ports to the outbuf...
     */

    int chn = 0;
    JSList *node = playback_ports;
    JSList *src_node = playback_srcs;
    SRC_DATA src;

    while ( node != NULL)
    {
	int i;
	jack_port_t *port = (jack_port_t *) node->data;
	float *buf = jack_port_get_buffer (port, nframes);

	SRC_STATE *src_state = src_node->data;

	src.data_in = buf;
	src.input_frames = nframes;

	src.data_out = resampbuf;
	src.output_frames = rlen;
	src.end_of_input = 0;

	src.src_ratio = current_resample_factor;

	src_process( src_state, &src );

	for (i=0; i < rlen; i++) {
	    float_16( resampbuf[i], outbuf[chn+ i*num_channels] );
	}

	src_node = jack_slist_next (src_node);
	node = jack_slist_next (node);
	chn++;
    }

    // now write the output...

again:
  err = snd_pcm_writei(alsa_handle, outbuf, src.output_frames_gen);
  if( err < 0 ) {
      printf( "err = %d\n", err );
      if (xrun_recovery(alsa_handle, err) < 0) {
	  //printf("Write error: %s\n", snd_strerror(err));
	  //exit(EXIT_FAILURE);
      }
      goto again;
  }

    return 0;      
}


/**
 * Allocate the necessary jack ports...
 */

void alloc_ports( int n_capture, int n_playback ) {

    int port_flags = JackPortIsOutput;
    int chn;
    jack_port_t *port;
    char buf[32];

    capture_ports = NULL;
    for (chn = 0; chn < n_capture; chn++)
    {
	snprintf (buf, sizeof(buf) - 1, "capture_%u", chn+1);

	port = jack_port_register (client, buf,
		JACK_DEFAULT_AUDIO_TYPE,
		port_flags, 0);

	if (!port)
	{
	    printf( "jacknet_client: cannot register port for %s", buf);
	    break;
	}

	capture_srcs = jack_slist_append( capture_srcs, src_new( SRC_SINC_FASTEST, 1, NULL ) );
	capture_ports = jack_slist_append (capture_ports, port);
    }

    port_flags = JackPortIsInput;

    playback_ports = NULL;
    for (chn = 0; chn < n_playback; chn++)
    {
	snprintf (buf, sizeof(buf) - 1, "playback_%u", chn+1);

	port = jack_port_register (client, buf,
		JACK_DEFAULT_AUDIO_TYPE,
		port_flags, 0);

	if (!port)
	{
	    printf( "jacknet_client: cannot register port for %s", buf);
	    break;
	}

	playback_srcs = jack_slist_append( playback_srcs, src_new( SRC_SINC_FASTEST, 1, NULL ) );
	playback_ports = jack_slist_append (playback_ports, port);
    }
}

/**
 * This is the shutdown callback for this JACK application.
 * It is called by JACK if the server ever shuts down or
 * decides to disconnect the client.
 */

void jack_shutdown (void *arg) {

	exit (1);
}

/**
 * be user friendly.
 * be user friendly.
 * be user friendly.
 */

void printUsage() {
fprintf(stderr, "usage: alsa_out [options]\n"
		"\n"
		"  -j <jack name> - reports a different name to jack\n"
		"  -d <alsa_device> \n"
		"  -c <channels> \n"
		"  -p <period_size> \n"
		"  -n <num_period> \n"
		"  -r <sample_rate> \n"
		"  -m <max_diff> \n"
		"  -t <target_delay> \n"
		"  -f <catch_factor> \n"
		"\n");
}


/**
 * the main function....
 */


int main (int argc, char *argv[]) {
    char jack_name[30] = "alsa_out";
    char alsa_device[30] = "hw:0";

    extern char *optarg;
    extern int optind, optopt;
    int errflg=0;
    int c;

    while ((c = getopt(argc, argv, ":j:r:c:p:n:d:m:t:f:")) != -1) {
	switch(c) {
	    case 'j':
		strcpy(jack_name,optarg);
		break;
	    case 'r':
		sample_rate = atoi(optarg);
		break;
	    case 'c':
		num_channels = atoi(optarg);
		break;
	    case 'p':
		period_size = atoi(optarg);
		break;
	    case 'n':
		num_periods = atoi(optarg);
		break;
	    case 'd':
		strcpy(alsa_device,optarg);
		break;
	    case 't':
		target_delay = atoi(optarg);
		break;
	    case 'm':
		max_diff = atoi(optarg);
		break;
	    case 'f':
		catch_factor = atoi(optarg);
		break;
	    case ':':
		fprintf(stderr,
			"Option -%c requires an operand\n", optopt);
		errflg++;
		break;
	    case '?':
		fprintf(stderr,
			"Unrecognized option: -%c\n", optopt);
		errflg++;
	}
    }
    if (errflg) {
	printUsage();
	exit(2);
    }

    // Setup target delay and max_diff for the normal user, who does not play with them...

    if( !target_delay ) 
	target_delay = num_periods*period_size / 2;

    if( !max_diff )
	max_diff = period_size / 2;	

    if ((client = jack_client_new (jack_name)) == 0) {
	fprintf (stderr, "jack server not running?\n");
	return 1;
    }

    /* tell the JACK server to call `process()' whenever
       there is work to be done.
       */

    jack_set_process_callback (client, process, 0);

    /* tell the JACK server to call `jack_shutdown()' if
       it ever shuts down, either entirely, or if it
       just decides to stop calling us.
       */

    jack_on_shutdown (client, jack_shutdown, 0);


    // alloc input ports, which are blasted out to alsa...
    alloc_ports( 0, num_channels );

    // get jack sample_rate
    
    jack_sample_rate = jack_get_sample_rate( client );

    if( !sample_rate )
	sample_rate = jack_sample_rate;

    current_resample_factor = (double) sample_rate / (double) jack_sample_rate;
    // now open the alsa fd...
    
    alsa_handle = open_audiofd( alsa_device, 0, sample_rate, num_channels, period_size, num_periods);
    if( alsa_handle < 0 )
	exit(20);
    

    /* tell the JACK server that we are ready to roll */

    if (jack_activate (client)) {
	fprintf (stderr, "cannot activate client");
	return 1;
    }

    while(1) {
	usleep(500000);
	if( output_new_delay ) {
	    printf( "delay = %d\n", output_new_delay );
	    output_new_delay = 0;
	}
	printf( "res: %f, \tdiff = %f, \toffset = %f \n", output_resampling_factor, output_diff, output_offset );
	
    }
    jack_client_close (client);
    exit (0);
}

