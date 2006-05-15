/*
    Copyright (C) 2001-2004 Paul Davis, Tilman Linneweh

    Generic version, overridden by OS-specific definition when needed.
    
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

    $Id$
*/

void jack_init_time ()
{
	/* nothing to do on a generic system - we use the system clock */
}
void jack_set_clock_source (jack_timer_type_t clocksrc) 
{
	/* only one clock source on a generic system */
}

