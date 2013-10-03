/* -*- Mode: C ; c-basic-offset: 2 -*- */
/*
 * ALSA SEQ < - > JACK MIDI bridge
 *
 * Copyright (c) 2006,2007 Dmitry S. Baikov <c0ff@konstruktiv.org>
 * Copyright (c) 2007,2008,2009 Nedko Arnaudov <nedko@arnaudov.name>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <stdbool.h>
#include <semaphore.h>
#include <alsa/asoundlib.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>

#include "list.h"
#include "a2j.h"
#include "port_hash.h"

static inline
int
a2j_port_hash(
  snd_seq_addr_t addr)
{
  return (addr.client + addr.port) % PORT_HASH_SIZE;
}

struct a2j_port *
a2j_port_get(
  a2j_port_hash_t hash,
  snd_seq_addr_t addr)
{
  struct a2j_port **pport = &hash[a2j_port_hash(addr)];
  while (*pport) {
    struct a2j_port *port = *pport;
    if (port->remote.client == addr.client && port->remote.port == addr.port)
      return port;
    pport = &port->next;
  }
  return NULL;
}

void
a2j_port_insert(
  a2j_port_hash_t hash,
  struct a2j_port * port)
{
  struct a2j_port **pport = &hash[a2j_port_hash(port->remote)];
  port->next = *pport;
  *pport = port;
}
