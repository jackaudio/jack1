#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sndfile.h>
#include <pthread.h>
#include <glib.h>
#include <getopt.h>

#include <jack/jack.h>

typedef struct _thread_info {
    pthread_t thread_id;
    SNDFILE *sf;
    nframes_t duration;
    jack_client_t *client;
    unsigned int channels;
    int bitdepth;
    int can_capture;
    char *path;
    int status;
    int process_go;
} thread_info_t;

unsigned int nports;
jack_port_t **ports;

pthread_mutex_t buffer_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  data_ready = PTHREAD_COND_INITIALIZER;

typedef struct _sample_buffer {
    nframes_t nframes;
    sample_t **data;
} sample_buffer_t;

sample_buffer_t *
sample_buffer_new (nframes_t nframes, unsigned int nchans)
{
	sample_buffer_t *buf;
	unsigned int i;

	buf = (sample_buffer_t *) malloc (sizeof (sample_buffer_t));
	buf->nframes = nframes;
	buf->data = (sample_t **) malloc (sizeof (sample_t *) * nchans);

	for (i = 0; i < nchans; i++) {
		buf->data[i] = (sample_t *) malloc (sizeof (sample_t) * nframes);
	}

	return buf;
}

GSList *pending_writes = NULL;
GSList *free_buffers = NULL;

sample_buffer_t *
get_free_buffer (nframes_t nframes, unsigned int nchans)
{
	sample_buffer_t *buf;

	if (free_buffers == NULL) {
		buf = sample_buffer_new (nframes, nchans);
	} else {
		buf = (sample_buffer_t *) free_buffers->data;
		free_buffers = g_slist_next (free_buffers);
	}

	return buf;
}

sample_buffer_t *
get_write_buffer ()
{
	sample_buffer_t *buf;

	if (pending_writes == NULL) {
		return NULL;
	} 

	buf = (sample_buffer_t *) pending_writes->data;
	pending_writes = g_slist_next (pending_writes);

	return buf;
}

void
put_write_buffer (sample_buffer_t *buf)
{
	pending_writes = g_slist_append (pending_writes, buf);
}

void
put_free_buffer (sample_buffer_t *buf)
{
	free_buffers = g_slist_prepend (free_buffers, buf);
}

void *
disk_thread (void *arg)
{
	sample_buffer_t *buf;
	thread_info_t *info = (thread_info_t *) arg;
	int i;
	unsigned int chn;
	nframes_t total_captured = 0;
	int done = 0;
	double *fbuf;

	pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	pthread_mutex_lock (&buffer_lock);

	/* preload the buffer cache */

	for (i = 0; i < 5; i++) {
		buf = sample_buffer_new (jack_get_buffer_size (info->client), info->channels);
		put_free_buffer (buf);
	}

	info->status = 0;

	while (!done) {
		pthread_cond_wait (&data_ready, &buffer_lock);

		while ((buf = get_write_buffer ()) != 0) {
			pthread_mutex_unlock (&buffer_lock);

			/* grrr ... libsndfile doesn't do float data yet, only double */
			
			if (info->can_capture) {

				fbuf = (double *) malloc (sizeof (double) * buf->nframes * info->channels);

				for (chn = 0; chn < info->channels; chn++) {
					for (i = 0; i < buf->nframes; i++) {
						fbuf[chn+(i*info->channels)] = buf->data[chn][i];
					}
				}
				
				if (sf_writef_double (info->sf, fbuf, buf->nframes, 1) != buf->nframes) {
					char errstr[256];
					sf_error_str (0, errstr, sizeof (errstr) - 1);
					fprintf (stderr, "cannot write data to sndfile (%s)\n", errstr);
					info->status = -1;
					done = 1;
					break;
				}
				
				free (fbuf);
				total_captured += buf->nframes;

				if (total_captured >= info->duration) {
					printf ("disk thread finished\n");
					done = 1;
					break;
				}
			}

			pthread_mutex_lock (&buffer_lock);
			put_free_buffer (buf);
		}
	}

	pthread_mutex_unlock (&buffer_lock);
	return 0;
}
	
int
process (nframes_t nframes, void *arg)

{
	thread_info_t *info = (thread_info_t *) arg;
	sample_t *in;
	sample_buffer_t *buf;
	unsigned int i;

	if (!info->process_go) {
		return 0;
	}

	pthread_mutex_lock (&buffer_lock);

	buf = get_free_buffer (nframes, nports);

	for (i = 0; i < nports; i++) {
		in = (sample_t *) jack_port_get_buffer (ports[i], nframes);
		memcpy (buf->data[i], in, sizeof (sample_t) * nframes);
	}

	/* we don't like taking locks, but until we have a lock
	   free ringbuffer written in C, this is what has to be done
	*/

	put_write_buffer (buf);

	/* tell the disk thread that there is work to do */
	
	pthread_cond_signal (&data_ready);
	pthread_mutex_unlock (&buffer_lock);

	return 0;      
}

void
jack_shutdown (void *arg)
{
	fprintf (stderr, "JACK shutdown\n");
	exit (0);
}

void
setup_disk_thread (thread_info_t *info)
{
	SF_INFO sf_info;

	sf_info.samplerate = jack_get_sample_rate (info->client);
	sf_info.channels = info->channels;
	sf_info.format = SF_FORMAT_WAV|SF_FORMAT_PCM;
	sf_info.pcmbitwidth = info->bitdepth;

	if ((info->sf = sf_open_write (info->path, &sf_info)) == NULL) {
		char errstr[256];
		sf_error_str (0, errstr, sizeof (errstr) - 1);
		fprintf (stderr, "cannot open sndfile \"%s\" for output (%s)\n", info->path, errstr);
		jack_client_close (info->client);
		exit (1);
	}

	info->duration *= sf_info.samplerate;
	info->can_capture = 0;

	pthread_create (&info->thread_id, NULL, disk_thread, info);
}

void
run_disk_thread (thread_info_t *info)
{
	info->can_capture = 1;
	pthread_join (info->thread_id, NULL);
	sf_close (info->sf);
	if (info->status) {
		unlink (info->path);
	}
}

void
setup_ports (int sources, char *source_names[], thread_info_t *info)
{
	int i;

	nports = sources;

	ports = (jack_port_t **) malloc (sizeof (jack_port_t *) * nports);

	for (i = 0; i < nports; i++) {
		char name[64];

		sprintf (name, "input%d", i+1);

		if ((ports[i] = jack_port_register (info->client, name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0)) == 0) {
			fprintf (stderr, "cannot register input port \"%s\"!\n", name);
			jack_client_close (info->client);
			exit (1);
		}
	}

	for (i = 0; i < nports; i++) {
		if (jack_port_connect (info->client, source_names[i], jack_port_name (ports[i]))) {
			fprintf (stderr, "cannot connect input port %s to %s\n", jack_port_name (ports[i]), source_names[i]);
			jack_client_close (info->client);
			exit (1);
		} 
	}

	info->process_go = 1;
}

int
main (int argc, char *argv[])

{
	jack_client_t *client;
	thread_info_t thread_info;
	int c;
	int longopt_index = 0;
	extern int optind, opterr;
	int show_usage = 0;
	char *optstring = "d:f:b:h";
	struct option long_options[] = {
		{ "help", 1, 0, 'h' },
		{ "duration", 1, 0, 'd' },
		{ "file", 1, 0, 'f' },
		{ "bitdepth", 1, 0, 'b' },
		{ 0, 0, 0, 0 }
	};

	memset (&thread_info, 0, sizeof (thread_info));
	opterr = 0;

	while ((c = getopt_long (argc, argv, optstring, long_options, &longopt_index)) != -1) {
		switch (c) {
		case 1:
			/* getopt signals end of '-' options */
			break;

		case 'h':
			show_usage++;
			break;
		case 'd':
			thread_info.duration = atoi (optarg);
			break;
		case 'f':
			thread_info.path = optarg;
			break;
		case 'b':
			thread_info.bitdepth = atoi (optarg);
			break;
		default:
			fprintf (stderr, "error\n");
			show_usage++;
			break;
		}
	}

	if (show_usage || thread_info.path == NULL || optind == argc) {
		fprintf (stderr, "usage: jackrec -f filename [ -d second ] [ -b bitdepth ] port1 [ port2 ... ]\n");
		exit (1);
	}

	if ((client = jack_client_new ("jackrec")) == 0) {
		fprintf (stderr, "jack server not running?\n");
		exit (1);
	}

	thread_info.client = client;
	thread_info.channels = argc - optind;
	thread_info.process_go = 0;

	setup_disk_thread (&thread_info);

	jack_set_process_callback (client, process, &thread_info);
	jack_on_shutdown (client, jack_shutdown, NULL);

	if (jack_activate (client)) {
		fprintf (stderr, "cannot activate client");
	}

	setup_ports (argc - optind, &argv[optind], &thread_info);

	run_disk_thread (&thread_info);

	jack_client_close (client);
	exit (0);
}
