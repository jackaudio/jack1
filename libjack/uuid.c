/*
    Copyright (C) 2013 Paul Davis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public License
    along with this program; if not, write to the Free Software 
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

*/

#include <stdio.h>
#include <stdint.h>

#include <jack/types.h>
#include <jack/uuid.h>

#include "internal.h"

static pthread_mutex_t uuid_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t        uuid_cnt = 0;

enum JackUUIDType {
        JackUUIDPort = 0x1,
        JackUUIDClient = 0x2
};

jack_uuid_t
jack_client_uuid_generate ()
{
        jack_uuid_t uuid = JackUUIDClient;
        pthread_mutex_lock (&uuid_lock);
        uuid = (uuid << 32) | ++uuid_cnt;
        pthread_mutex_unlock (&uuid_lock);
        return uuid;
}

jack_uuid_t
jack_port_uuid_generate (uint32_t port_id)
{
        jack_uuid_t uuid = JackUUIDPort;
        uuid = (uuid << 32) | (port_id + 1);
        return uuid;
}

uint32_t
jack_uuid_to_index (jack_uuid_t u)
{
        return (u & 0xffff) - 1;
}

int
jack_uuid_empty (jack_uuid_t u)
{
        return (u == 0);
}

int
jack_uuid_compare (jack_uuid_t a, jack_uuid_t b)
{
        if (a == b) { 
                return 0;
        }

        if (a < b) {
                return -1;
        } 

        return 1;
}

void
jack_uuid_copy (jack_uuid_t* dst, jack_uuid_t src)
{
        *dst = src;
}

void
jack_uuid_clear (jack_uuid_t* u)
{
        *u = 0;
}

void
jack_uuid_unparse (jack_uuid_t u, char b[JACK_UUID_STRING_SIZE])
{
        snprintf (b, JACK_UUID_STRING_SIZE, "%" PRIu64, u);
}

int
jack_uuid_parse (const char *b, jack_uuid_t* u)
{
        if (sscanf (b, "%" PRIu64, u) == 1) {

                if (*u < (0x1LL << 32)) {
                        /* has not type bits set - not legal */
                        return -1;
                }

                return 0;
        }

        return -1;
}
