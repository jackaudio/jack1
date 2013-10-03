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

#ifndef PORT_THREAD_H__1C6B5065_5556_4AC6_AA9F_44C32A9648C6__INCLUDED
#define PORT_THREAD_H__1C6B5065_5556_4AC6_AA9F_44C32A9648C6__INCLUDED

void
a2j_update_port(
  struct a2j * self,
  snd_seq_addr_t addr,
  const snd_seq_port_info_t * info);

void
a2j_update_ports(
  struct a2j * self);

void
a2j_free_ports(
  jack_ringbuffer_t * ports);

struct a2j_port *
a2j_find_port_by_addr(
  struct a2j_stream * stream_ptr,
  snd_seq_addr_t addr);

struct a2j_port *
a2j_find_port_by_jack_port_name(
  struct a2j_stream * stream_ptr,
  const char * jack_port);

#endif /* #ifndef PORT_THREAD_H__1C6B5065_5556_4AC6_AA9F_44C32A9648C6__INCLUDED */
