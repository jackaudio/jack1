/*
 *  Copyright (C) 2004 Rui Nuno Capela, Steve Harris
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2.1 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *  
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software 
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  $Id$
 */

#ifndef __jack_messagebuffer_h__
#define __jack_messagebuffer_h__

#define MB_BUFFERSIZE	256

#define MESSAGE(fmt,args...) {			\
		char msg[MB_BUFFERSIZE];		\
		snprintf(msg, MB_BUFFERSIZE-1, fmt, ##args); \
		jack_messagebuffer_add(msg);	\
	}

#define VERBOSE(engine,fmt,args...)		\
	if ((engine)->verbose) {			\
		char msg[MB_BUFFERSIZE];		\
		snprintf(msg, MB_BUFFERSIZE-1, fmt, ##args); \
		jack_messagebuffer_add(msg);	\
	}

void jack_messagebuffer_init(const char *prefix);
void jack_messagebuffer_exit();

void jack_messagebuffer_add(const char *msg);

#endif /* __jack_messagebuffer_h__ */
