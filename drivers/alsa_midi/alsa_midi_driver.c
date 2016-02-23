
#include "alsa_midi.h"
#include <string.h>

static int
alsa_midi_driver_attach ( alsa_midi_driver_t *driver, jack_engine_t *engine )
{
	return driver->midi->attach (driver->midi);
}

static int
alsa_midi_driver_detach ( alsa_midi_driver_t *driver, jack_engine_t *engine )
{
	return driver->midi->detach (driver->midi);
}

static int
alsa_midi_driver_read ( alsa_midi_driver_t *driver, jack_nframes_t nframes )
{
	driver->midi->read (driver->midi, nframes);
	return 0;
}

static int
alsa_midi_driver_write ( alsa_midi_driver_t *driver, jack_nframes_t nframes )
{
	driver->midi->write (driver->midi, nframes);
	return 0;
}

static int
alsa_midi_driver_start ( alsa_midi_driver_t *driver )
{
	return driver->midi->start (driver->midi);
}

static int
alsa_midi_driver_stop ( alsa_midi_driver_t *driver )
{
	return driver->midi->stop (driver->midi);
}

static void
alsa_midi_driver_delete ( alsa_midi_driver_t *driver )
{

}

