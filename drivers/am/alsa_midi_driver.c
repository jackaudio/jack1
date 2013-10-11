
#include "alsa_midi.h"
#include <string.h>

static int
alsa_midi_driver_attach( alsa_midi_driver_t *driver, jack_engine_t *engine )
{
	return driver->midi->attach(driver->midi);
}

static int
alsa_midi_driver_detach( alsa_midi_driver_t *driver, jack_engine_t *engine )
{
	return driver->midi->detach(driver->midi);
}

static int
alsa_midi_driver_read( alsa_midi_driver_t *driver, jack_nframes_t nframes )
{
	driver->midi->read(driver->midi, nframes);
	return 0;
}

static int
alsa_midi_driver_write( alsa_midi_driver_t *driver, jack_nframes_t nframes )
{
	driver->midi->write(driver->midi, nframes);
	return 0;
}

static int
alsa_midi_driver_start( alsa_midi_driver_t *driver )
{
	return driver->midi->start(driver->midi);
}

static int
alsa_midi_driver_stop( alsa_midi_driver_t *driver )
{
	return driver->midi->stop(driver->midi);
}

static void
alsa_midi_driver_delete( alsa_midi_driver_t *driver )
{
	if (driver->midi)
		(driver->midi->destroy)(driver->midi);

	free (driver);
}

static jack_driver_t *
alsa_midi_driver_new (jack_client_t *client, const char *name)
{
	alsa_midi_driver_t *driver;

	jack_info ("creating alsa_midi driver ..."); 

	driver = (alsa_midi_driver_t *) calloc (1, sizeof (alsa_midi_driver_t));

	jack_driver_init ((jack_driver_t *) driver);

	driver->attach = (JackDriverAttachFunction) alsa_midi_driver_attach;
	driver->detach = (JackDriverDetachFunction) alsa_midi_driver_detach;
	driver->read = (JackDriverReadFunction) alsa_midi_driver_read;
	driver->write = (JackDriverWriteFunction) alsa_midi_driver_write;
	driver->start = (JackDriverStartFunction) alsa_midi_driver_start;
	driver->stop = (JackDriverStartFunction) alsa_midi_driver_stop;


	driver->midi   = alsa_seqmidi_new(client, NULL);
	driver->client = client;

	return (jack_driver_t *) driver;
}

/* DRIVER "PLUGIN" INTERFACE */

const char driver_client_name[] = "alsa_midi";

const jack_driver_desc_t *
driver_get_descriptor ()
{
	jack_driver_desc_t * desc;
	jack_driver_param_desc_t * params;
	//unsigned int i;

	desc = calloc (1, sizeof (jack_driver_desc_t));

	strcpy (desc->name,"alsa_midi");
	desc->nparams = 0;
  
	params = calloc (desc->nparams, sizeof (jack_driver_param_desc_t));

	desc->params = params;

	return desc;
}

jack_driver_t *
driver_initialize (jack_client_t *client, const JSList * params)
{
	const JSList * node;
	const jack_driver_param_t * param;

	for (node = params; node; node = jack_slist_next (node)) {
  	        param = (const jack_driver_param_t *) node->data;

		switch (param->character) {
			default:
				break;
		}
	}
			
	return alsa_midi_driver_new (client, NULL);
}

void
driver_finish (jack_driver_t *driver)
{
	alsa_midi_driver_delete ((alsa_midi_driver_t *) driver);
}
