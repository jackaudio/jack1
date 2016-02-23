/*
    Copyright © Grame 2003

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

    Grame Research Laboratory, 9, rue du Garet 69001 Lyon - France
    grame@rd.grame.fr
 */

#ifndef __mach_port__
#define __mach_port__

#include <mach/mach.h>
#include <mach/mach_types.h>
#include <mach/message.h>

/* specific ressources for server/client real-time thread communication */
typedef struct {
	mach_msg_header_t header;
	mach_msg_trailer_t trailer;
} trivial_message;

#endif