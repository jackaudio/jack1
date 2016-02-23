/*
    Copyright (C) 2003 Robert Ham <rah@bash.sh>
    Copyright (C) 2005 Torben Hohn <torbenh@gmx.de>

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


#ifndef __JACK_NET_DRIVER_H__
#define __JACK_NET_DRIVER_H__

#include <unistd.h>

#include <jack/types.h>
#include <jack/jack.h>
#include <jack/transport.h>

#include "driver.h"

#include <netinet/in.h>

#include "netjack.h"

typedef struct _net_driver net_driver_t;

struct _net_driver {
	JACK_DRIVER_NT_DECL;

	netjack_driver_state_t netj;
};

#endif /* __JACK_NET_DRIVER_H__ */
