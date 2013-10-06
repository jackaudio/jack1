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

#include <jack/types.h>
#include <jack/uuid.h>

#include "internal.h"

void
jack_uuid_generate (jack_uuid_t uuid)
{
        uuid_generate (uuid);
}

int
jack_uuid_empty (const jack_uuid_t u)
{
        jack_uuid_t empty;
        VALGRIND_MEMSET(&empty, 0, sizeof(empty));
        uuid_clear (empty);
        if (jack_uuid_compare (u, empty) == 0) {
                return 1;
        }
        return 0;
}

int
jack_uuid_compare (const jack_uuid_t a, const jack_uuid_t b)
{
        return uuid_compare (a, b);
}

void
jack_uuid_copy (jack_uuid_t a, const jack_uuid_t b)
{
        uuid_copy (a, b);
}

void
jack_uuid_clear (jack_uuid_t u)
{
        uuid_clear (u);
}

void
jack_uuid_unparse (const jack_uuid_t u, char b[JACK_UUID_STRING_SIZE])
{
        uuid_unparse (u, b);
}

int
jack_uuid_parse (const char *b, jack_uuid_t u)
{
        return uuid_parse (b, u);
}
