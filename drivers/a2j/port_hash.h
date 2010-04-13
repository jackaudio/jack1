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

#ifndef PORT_HASH_H__A44CBCD6_E075_49CB_8F73_DF9772511D55__INCLUDED
#define PORT_HASH_H__A44CBCD6_E075_49CB_8F73_DF9772511D55__INCLUDED

void
a2j_port_insert(
  a2j_port_hash_t hash,
  struct a2j_port * port);

struct a2j_port *
a2j_port_get(
  a2j_port_hash_t hash,
  snd_seq_addr_t addr);

#endif /* #ifndef PORT_HASH_H__A44CBCD6_E075_49CB_8F73_DF9772511D55__INCLUDED */
