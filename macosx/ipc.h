/*
    Copyright © Grame 2003

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
    
    Grame Research Laboratory, 9, rue du Garet 69001 Lyon - France
    grame@rd.grame.fr
*/

#ifndef __ipc__
#define __ipc__

#include <jack/internal.h>
#include <jack/engine.h>
#include <local.h>

int jack_client_resume(jack_client_internal_t *client);
int jack_client_suspend(jack_client_t * client);

void allocate_mach_serverport(jack_engine_t * engine, jack_client_internal_t *client);
int allocate_mach_clientport(jack_client_t * client, int portnum);

typedef int socklen_t;

#endif