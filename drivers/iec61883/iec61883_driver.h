/*
 *   JACK IEC16883 (FireWire audio) driver
 *
 *   Copyright (C) Robert Ham 2003 (rah@bash.sh)
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __JACK_IEC61883_DRIVER_H__
#define __JACK_IEC61883_DRIVER_H__

#include <jack/driver.h>
#include <jack/engine.h>
#include <jack/types.h>

#include "iec61883_client.h"

typedef struct _iec61883_driver iec61883_driver_t;

struct _iec61883_driver
{
  JACK_DRIVER_NT_DECL
  
  jack_client_t *     jack_client;
  
  jack_nframes_t      buffer_size;
  iec61883_client_t * iec61883_client;
};



#endif /* __JACK_IEC61883_DRIVER_H__ */


