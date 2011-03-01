/*
    Copyright (C) 2003-2008 Fons Adriaensen <fons@kokkinizita.net>
    
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
*/

// --------------------------------------------------------------------------------

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <jack/jack.h>

struct Freq
{
    int   p;
    int   f;
    float a;
    float xa;
    float ya;
    float xf;
    float yf;
};

struct MTDM
{
    double  _del;
    double  _err;
    int     _cnt;
    int     _inv;
    struct Freq    _freq [5];
};

struct MTDM * mtdm_new (void)
{
    int   i;
    struct Freq  *F;

    struct MTDM *retval = malloc( sizeof(struct MTDM) );

    if (retval==NULL)
	    return NULL;

    retval->_cnt = 0;
    retval->_inv = 0;

    retval->_freq [0].f = 4096;
    retval->_freq [1].f =  512;
    retval->_freq [2].f = 1088;
    retval->_freq [3].f = 1544;
    retval->_freq [4].f = 2049;

    retval->_freq [0].a = 0.2f;
    retval->_freq [1].a = 0.1f;
    retval->_freq [2].a = 0.1f;
    retval->_freq [3].a = 0.1f;
    retval->_freq [4].a = 0.1f;

    for (i = 0, F = retval->_freq; i < 5; i++, F++)
    {
	F->p = 128;
	F->xa = F->ya = 0.0f;
	F->xf = F->yf = 0.0f;
    }

    return retval;
}

int mtdm_process (struct MTDM *self, size_t len, float *ip, float *op)
{
    int    i;
    float  vip, vop, a, c, s;
    struct Freq   *F;

    while (len--)
    {
        vop = 0.0f;
	vip = *ip++;
	for (i = 0, F = self->_freq; i < 5; i++, F++)
	{
	    a = 2 * (float) M_PI * (F->p & 65535) / 65536.0; 
	    F->p += F->f;
	    c =  cosf (a); 
	    s = -sinf (a); 
	    vop += F->a * s;
	    F->xa += s * vip;
	    F->ya += c * vip;
	} 
	*op++ = vop;
	if (++(self->_cnt) == 16)
	{
	    for (i = 0, F = self->_freq; i < 5; i++, F++)
	    {
		F->xf += 1e-3f * (F->xa - F->xf + 1e-20);
		F->yf += 1e-3f * (F->ya - F->yf + 1e-20);
		F->xa = F->ya = 0.0f;
	    }
            self->_cnt = 0;
	}
    }

    return 0;
}

int mtdm_resolve (struct MTDM *self)
{
    int     i, k, m;
    double  d, e, f0, p;
    struct Freq    *F = self->_freq;

    if (hypot (F->xf, F->yf) < 0.01) return -1;
    d = atan2 (F->yf, F->xf) / (2 * M_PI);
    if (self->_inv) d += 0.5f;
    if (d > 0.5f) d -= 1.0f;
    f0 = self->_freq [0].f;
    m = 1;
    self->_err = 0.0;
    for (i = 0; i < 4; i++)
    {
	F++;
	p = atan2 (F->yf, F->xf) / (2 * M_PI) - d * F->f / f0;
        if (self->_inv) p += 0.5f;
	p -= floor (p);
	p *= 8;
	k = (int)(floor (p + 0.5));
	e = fabs (p - k);
        if (e > self->_err) self->_err = e;
        if (e > 0.4) return 1; 
	d += m * (k & 7);
	m *= 8;
    }  
    self->_del = 16 * d;

    return 0;
}

void mtdm_invert (struct MTDM *self)
{
	self->_inv ^= 1;
}
// --------------------------------------------------------------------------------

static struct MTDM    *mtdm;
static jack_client_t  *jack_handle;
static jack_port_t    *jack_capt;
static jack_port_t    *jack_play;

jack_latency_range_t   capture_latency = {-1, -1};
jack_latency_range_t   playback_latency = {-1, -1};

void
latency_cb (jack_latency_callback_mode_t mode, void *arg)
{
	jack_latency_range_t range;

	range.min = range.max = 0;

	if (mode == JackCaptureLatency) {
		jack_port_set_latency_range (jack_play, mode, &range);
		jack_port_get_latency_range (jack_capt, mode, &range);
		if ((range.min != capture_latency.min) || (range.max != capture_latency.max)) {
			capture_latency = range;
			printf ("new capture latency: [%d, %d]\n", range.min, range.max);
		}
	} else {
		jack_port_set_latency_range (jack_capt, mode, &range);
		jack_port_get_latency_range (jack_play, mode, &range);
		if ((range.min != playback_latency.min) || (range.max != playback_latency.max)) {
			playback_latency = range;
			printf ("new playback latency: [%d, %d]\n", range.min, range.max);
		}
	}

}

int jack_callback (jack_nframes_t nframes, void *arg)
{
    float *ip, *op;

    ip = (float *)(jack_port_get_buffer (jack_capt, nframes));
    op = (float *)(jack_port_get_buffer (jack_play, nframes));
    mtdm_process (mtdm, nframes, ip, op);
    return 0;
}

int main (int ac, char *av [])
{
    float          t;
    jack_status_t  s;

    mtdm = mtdm_new();

    jack_handle = jack_client_open ("jack_delay", JackNoStartServer, &s);
    if (jack_handle == 0)
    {
        fprintf (stderr, "Can't connect to Jack, is the server running ?\n");
        exit (1);
    }

    jack_set_process_callback (jack_handle, jack_callback, 0);

    if (jack_set_latency_callback)
	    jack_set_latency_callback (jack_handle, latency_cb, 0);

    jack_capt = jack_port_register (jack_handle, "in",  JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    jack_play = jack_port_register (jack_handle, "out", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

    t = 1000.0f / jack_get_sample_rate (jack_handle);

    if (jack_activate (jack_handle))
    {
        fprintf(stderr, "Can't activate Jack");
        return 1;
    }

    while (1)
    {
 
    #ifdef WIN32 
        Sleep (250); 
    #else 
        usleep (250000); 
 	#endif        
        if (mtdm_resolve (mtdm) < 0) printf ("Signal below threshold...\n");
        else 
        {
            if (mtdm->_err > 0.3) 
            {
                mtdm_invert ( mtdm );
                mtdm_resolve ( mtdm );
            }
            printf ("%10.3lf frames %10.3lf ms", mtdm->_del, mtdm->_del * t);
            if (mtdm->_err > 0.2) printf (" ??");
                if (mtdm->_inv) printf (" Inv");
            printf ("\n");
        }
    }

    return 0;
}

// --------------------------------------------------------------------------------
