/*
    JACK transport client interface -- runs in the client process.

    Copyright (C) 2001-2003 Paul Davis
    Copyright (C) 2003 Jack O'Quin
    
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

#include <config.h>
#include <jack/internal.h>
#include "local.h"


#ifdef OLD_TRANSPORT

/* * * API functions for compatibility with old transport interface * * */

int
jack_engine_takeover_timebase (jack_client_t *client)

{
	jack_request_t req;

	req.type = SetTimeBaseClient;
	req.x.client_id = client->control->id;

	return jack_client_deliver_request (client, &req);
}	

void
jack_get_transport_info (jack_client_t *client,
			 jack_transport_info_t *info)
{
	*info = client->engine->current_time;
}

void
jack_set_transport_info (jack_client_t *client,
			 jack_transport_info_t *info)
{
	client->engine->pending_time = *info;
}	

#endif /* OLD_TRANSPORT */
