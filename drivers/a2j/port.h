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

#ifndef PORT_H__757ADD0F_5E53_41F7_8B7F_8119C5E8A9F1__INCLUDED
#define PORT_H__757ADD0F_5E53_41F7_8B7F_8119C5E8A9F1__INCLUDED

struct a2j_port* a2j_port_create (struct a2j * self, snd_seq_addr_t addr, const snd_seq_port_info_t * info);
void a2j_port_setdead (a2j_port_hash_t hash, snd_seq_addr_t addr);
void a2j_port_free (struct a2j_port * port);

#endif /* #ifndef PORT_H__757ADD0F_5E53_41F7_8B7F_8119C5E8A9F1__INCLUDED */
