/*
    Copyright (C) 2001 Paul Davis
    
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

    $Id$
*/

#ifndef __audioengine_port_h__
#endif  __audioengine_port_h__

#define AUDIOENGINE_PORT_NAME_SIZE 32
#define AUDIOENGINE_PORT_TYPE_SIZE 32

struct _audioengine_port_t {

    /* clients can use this value, and only this value */

    void                      *buffer;      

    /* the rest of this is private, for use by the engine only */

    unsigned long              flags; 
    GSList                    *connections;
    void                      *own_buffer;
    audioengine_port_t        *tied;
    unsigned long              buffer_size;
    char                       name[AUDIOENGINE_PORT_NAME_SIZE+1];
    char                       type[AUDIOENGINE_PORT_TYPE_SIZE+1];
    char                       client[AUDIOENGINE_CLIENT_NAME_SIZE+1];
    pthread_mutex_t            lock;
    audioengine_port_id_t      id;
    unsigned long              client_id;
    char                       in_use : 1;
    char                       builtin : 1;
    char                       locked : 1;
};

#endif /* __audioengine_port_h__ */
