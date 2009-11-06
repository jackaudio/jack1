/* -*- mode: c; c-file-style: "linux"; -*- */
/*
NetJack Driver

Copyright (C) 2008 Pieter Palmers <pieterpalmers@users.sourceforge.net>
Copyright (C) 2006 Torben Hohn <torbenh@gmx.de>
Copyright (C) 2003 Robert Ham <rah@bash.sh>
Copyright (C) 2001 Paul Davis

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

$Id: net_driver.c,v 1.17 2006/04/16 20:16:10 torbenh Exp $
*/

#include <math.h>
#include <stdio.h>
#include <memory.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/mman.h>

#include <jack/types.h>
#include <jack/engine.h>
#include <sysdeps/time.h>

#include <sys/types.h>

#include "config.h"


#include "netjack.h"
#include "net_driver.h"

#undef DEBUG_WAKEUP



static jack_nframes_t
net_driver_wait (net_driver_t *driver, int extra_fd, int *status, float *delayed_usecs)
{
    netjack_driver_state_t *netj = &( driver->netj );

    netjack_wait( netj );
    
    driver->last_wait_ust = jack_get_microseconds ();
    driver->engine->transport_cycle_start (driver->engine, driver->last_wait_ust);

    /* this driver doesn't work so well if we report a delay */
    /* XXX: this might not be the case anymore */
    /*      the delayed _usecs is a resync or something. */
    *delayed_usecs = 0;		/* lie about it */
    *status = 0;
    return netj->period_size;
}

static int
net_driver_run_cycle (net_driver_t *driver)
{
    jack_engine_t *engine = driver->engine;
    //netjack_driver_state_t *netj = &(driver->netj);
    int wait_status = -1;
    float delayed_usecs;

    jack_nframes_t nframes = net_driver_wait (driver, -1, &wait_status,
                             &delayed_usecs);

    // XXX: xrun code removed.
    //      especially with celt there are no real xruns anymore.
    //      things are different on the net.

    if (wait_status == 0)
        return engine->run_cycle (engine, nframes, delayed_usecs);

    if (wait_status < 0)
        return -1;
    else
        return 0;
}

static int
net_driver_null_cycle (net_driver_t* driver, jack_nframes_t nframes)
{
    // TODO: talk to paul about this.
    //       do i wait here ?
    //       just sending out a packet marked with junk ?

    netjack_driver_state_t *netj = &(driver->netj);
    int sync_state = (driver->engine->control->sync_remain <= 1);
    netjack_send_silence( netj, sync_state );

    return 0;
}

static int
net_driver_bufsize (net_driver_t* driver, jack_nframes_t nframes)
{
    netjack_driver_state_t *netj = &(driver->netj);
    if (nframes != netj->period_size)
        return EINVAL;

    return 0;
}

static int
net_driver_read (net_driver_t* driver, jack_nframes_t nframes)
{
    netjack_driver_state_t *netj = &(driver->netj);

    netjack_read( netj, nframes );
    return 0;
}

static int
net_driver_write (net_driver_t* driver, jack_nframes_t nframes)
{
    netjack_driver_state_t *netj = &(driver->netj);

    int sync_state = (driver->engine->control->sync_remain <= 1);;
    netjack_write( netj, nframes, sync_state );
    return 0;
}


static int
net_driver_attach (net_driver_t *driver)
{
    netjack_driver_state_t *netj = &( driver->netj );
    driver->engine->set_buffer_size (driver->engine, netj->period_size);
    driver->engine->set_sample_rate (driver->engine, netj->sample_rate);

    netjack_attach( netj );
    return 0;
}

static int
net_driver_detach (net_driver_t *driver)
{
    netjack_driver_state_t *netj = &( driver->netj );

    if (driver->engine == 0)
        return 0;

    netjack_detach( netj );
    return 0;
}

static void
net_driver_delete (net_driver_t *driver)
{
    jack_driver_nt_finish ((jack_driver_nt_t *) driver);
    free (driver);
}

static jack_driver_t *
net_driver_new (jack_client_t * client,
                char *name,
                unsigned int capture_ports,
                unsigned int playback_ports,
                unsigned int capture_ports_midi,
                unsigned int playback_ports_midi,
                jack_nframes_t sample_rate,
                jack_nframes_t period_size,
                unsigned int listen_port,
                unsigned int transport_sync,
                unsigned int resample_factor,
                unsigned int resample_factor_up,
                unsigned int bitdepth,
		unsigned int use_autoconfig,
		unsigned int latency,
		unsigned int redundancy,
		int dont_htonl_floats,
		int always_deadline)
{
    net_driver_t * driver;
    netjack_driver_state_t *netj = &(driver->netj);

    jack_info ("creating net driver ... %s|%" PRIu32 "|%" PRIu32
            "|%u|%u|%u|transport_sync:%u", name, sample_rate, period_size, listen_port,
            capture_ports, playback_ports, transport_sync);

    driver = (net_driver_t *) calloc (1, sizeof (net_driver_t));

    jack_driver_nt_init ((jack_driver_nt_t *) driver);

    driver->write         = (JackDriverWriteFunction)      net_driver_write;
    driver->read          = (JackDriverReadFunction)       net_driver_read;
    driver->null_cycle    = (JackDriverNullCycleFunction)  net_driver_null_cycle;
    driver->nt_attach     = (JackDriverNTAttachFunction)   net_driver_attach;
    driver->nt_detach     = (JackDriverNTDetachFunction)   net_driver_detach;
    driver->nt_bufsize    = (JackDriverNTBufSizeFunction)  net_driver_bufsize;
    driver->nt_run_cycle  = (JackDriverNTRunCycleFunction) net_driver_run_cycle;

    driver->last_wait_ust = 0;
    driver->engine = NULL;


    netjack_init ( netj,
		client,
                name,
                capture_ports,
                playback_ports,
                capture_ports_midi,
                playback_ports_midi,
                sample_rate,
                period_size,
                listen_port,
                transport_sync,
                resample_factor,
                resample_factor_up,
                bitdepth,
		use_autoconfig,
		latency,
		redundancy,
		dont_htonl_floats,
	        always_deadline	);


    jack_info ("netjack: period   : up: %d / dn: %d", netj->net_period_up, netj->net_period_down);
    jack_info ("netjack: framerate: %d", netj->sample_rate);
    jack_info ("netjack: audio    : cap: %d / pbk: %d)", netj->capture_channels_audio, netj->playback_channels_audio);
    jack_info ("netjack: midi     : cap: %d / pbk: %d)", netj->capture_channels_midi, netj->playback_channels_midi);
    jack_info ("netjack: buffsize : rx: %d)", netj->rx_bufsize);
    return (jack_driver_t *) driver;
}

/* DRIVER "PLUGIN" INTERFACE */

jack_driver_desc_t *
driver_get_descriptor ()
{
    jack_driver_desc_t * desc;
    jack_driver_param_desc_t * params;
    unsigned int i;

    desc = calloc (1, sizeof (jack_driver_desc_t));
    strcpy (desc->name, "net");
    desc->nparams = 17;

    params = calloc (desc->nparams, sizeof (jack_driver_param_desc_t));

    i = 0;
    strcpy (params[i].name, "inchannels");
    params[i].character  = 'i';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 2U;
    strcpy (params[i].short_desc, "Number of capture channels (defaults to 2)");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "outchannels");
    params[i].character  = 'o';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 2U;
    strcpy (params[i].short_desc, "Number of playback channels (defaults to 2)");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "midi inchannels");
    params[i].character  = 'I';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 1U;
    strcpy (params[i].short_desc, "Number of midi capture channels (defaults to 1)");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "midi outchannels");
    params[i].character  = 'O';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 1U;
    strcpy (params[i].short_desc, "Number of midi playback channels (defaults to 1)");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "rate");
    params[i].character  = 'r';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 48000U;
    strcpy (params[i].short_desc, "Sample rate");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "period");
    params[i].character  = 'p';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 1024U;
    strcpy (params[i].short_desc, "Frames per period");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "listen-port");
    params[i].character  = 'l';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 3000U;
    strcpy (params[i].short_desc,
            "The socket port we are listening on for sync packets");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "factor");
    params[i].character  = 'f';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 1U;
    strcpy (params[i].short_desc,
            "Factor for sample rate reduction");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "upstream-factor");
    params[i].character  = 'u';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 0U;
    strcpy (params[i].short_desc,
            "Factor for sample rate reduction on the upstream");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "celt");
    params[i].character  = 'c';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 0U;
    strcpy (params[i].short_desc,
            "sets celt encoding and number of bytes per channel");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "bit-depth");
    params[i].character  = 'b';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 0U;
    strcpy (params[i].short_desc,
            "Sample bit-depth (0 for float, 8 for 8bit and 16 for 16bit)");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "transport-sync");
    params[i].character  = 't';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 1U;
    strcpy (params[i].short_desc,
            "Whether to slave the transport to the master transport");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "autoconf");
    params[i].character  = 'a';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 1U;
    strcpy (params[i].short_desc,
            "Whether to use Autoconfig, or just start.");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "latency");
    params[i].character  = 'L';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 5U;
    strcpy (params[i].short_desc,
            "Latency setting");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "redundancy");
    params[i].character  = 'R';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 1U;
    strcpy (params[i].short_desc,
            "Send packets N times");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "no-htonl");
    params[i].character  = 'H';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 0U;
    strcpy (params[i].short_desc,
            "Dont convert samples to network byte order.");
    strcpy (params[i].long_desc, params[i].short_desc);

    i++;
    strcpy (params[i].name, "always-deadline");
    params[i].character  = 'D';
    params[i].type       = JackDriverParamUInt;
    params[i].value.ui   = 0U;
    strcpy (params[i].short_desc,
            "Always wait until deadline");
    strcpy (params[i].long_desc, params[i].short_desc);
    desc->params = params;

    return desc;
}

const char driver_client_name[] = "net_pcm";

jack_driver_t *
driver_initialize (jack_client_t *client, const JSList * params)
{
    jack_nframes_t sample_rate = 48000;
    jack_nframes_t resample_factor = 1;
    jack_nframes_t period_size = 1024;
    unsigned int capture_ports = 2;
    unsigned int playback_ports = 2;
    unsigned int capture_ports_midi = 1;
    unsigned int playback_ports_midi = 1;
    unsigned int listen_port = 3000;
    unsigned int resample_factor_up = 0;
    unsigned int bitdepth = 0;
    unsigned int handle_transport_sync = 1;
    unsigned int use_autoconfig = 1;
    unsigned int latency = 5;
    unsigned int redundancy = 1;
    int dont_htonl_floats = 0;
    int always_deadline = 0;
    const JSList * node;
    const jack_driver_param_t * param;

    for (node = params; node; node = jack_slist_next (node)) {
        param = (const jack_driver_param_t *) node->data;

        switch (param->character) {

            case 'i':
                capture_ports = param->value.ui;
                break;

            case 'o':
                playback_ports = param->value.ui;
                break;

            case 'I':
                capture_ports_midi = param->value.ui;
                break;

            case 'O':
                playback_ports_midi = param->value.ui;
                break;

            case 'r':
                sample_rate = param->value.ui;
                break;

            case 'p':
                period_size = param->value.ui;
                break;

            case 'l':
                listen_port = param->value.ui;
                break;

            case 'f':
#if HAVE_SAMPLERATE
                resample_factor = param->value.ui;
#else
		printf( "not built with libsamplerate support\n" );
		exit(10);
#endif
                break;

            case 'u':
#if HAVE_SAMPLERATE
                resample_factor_up = param->value.ui;
#else
		printf( "not built with libsamplerate support\n" );
		exit(10);
#endif
                break;

            case 'b':
                bitdepth = param->value.ui;
                break;

	    case 'c':
#if HAVE_CELT
		bitdepth = 1000;
		resample_factor = param->value.ui;
#else
		printf( "not built with celt support\n" );
		exit(10);
#endif
		break;

            case 't':
                handle_transport_sync = param->value.ui;
                break;

            case 'a':
                use_autoconfig = param->value.ui;
                break;

            case 'L':
                latency = param->value.ui;
                break;

            case 'R':
                redundancy = param->value.ui;
                break;

            case 'H':
                dont_htonl_floats = param->value.ui;
                break;
            case 'D':
                always_deadline = param->value.ui;
                break;
        }
    }

    return net_driver_new (client, "net_pcm", capture_ports, playback_ports,
                           capture_ports_midi, playback_ports_midi,
                           sample_rate, period_size,
                           listen_port, handle_transport_sync,
                           resample_factor, resample_factor_up, bitdepth,
			   use_autoconfig, latency, redundancy,
			   dont_htonl_floats, always_deadline);
}

void
driver_finish (jack_driver_t *driver)
{
    net_driver_delete ((net_driver_t *) driver);
}
