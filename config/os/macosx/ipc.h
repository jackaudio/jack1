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

#ifndef __ipc__
#define __ipc__

#include <mach/mach_error.h>
#include <servers/bootstrap.h>
#include <jack/internal.h>
#include <jack/engine.h>
#include <libjack/local.h>		/* JOQ: fix me */

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

/*
    RPC without time out can put the jack server in a blocked state
    (waiting for the client answer) when a client is killed.  The
    mach_msg function does not return any error in this case. Using
    time out solve the problem but does not seems really satisfactory.
*/

#define WAIT 25 /* in millisecond */

static inline int 
jack_client_resume(jack_client_internal_t *client)
{
        mach_msg_header_t *head =  &client->message.header;
        int err;
        
        if (!client->running) {
            err = mach_msg (head, MACH_RCV_MSG, 0, sizeof(client->message),
			    client->serverport, 0, MACH_PORT_NULL);
            if (err) {
                jack_error("jack_client_resume: priming receive error: %s\n", 
			   mach_error_string(err));
                return -1;
            }
            client->running = TRUE;
        }else {
            /* remote port is already the send-once he sent us */
            head->msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
            head->msgh_local_port = MACH_PORT_NULL;
            head->msgh_size = sizeof(mach_msg_header_t);
    
            err = mach_msg(head, (MACH_SEND_MSG|MACH_RCV_MSG|
				  MACH_SEND_TIMEOUT|MACH_RCV_TIMEOUT), 
			   sizeof(*head), sizeof(client->message),
			   client->serverport, WAIT, MACH_PORT_NULL);
                
            if (err) {
            
                    /*
                    switch(err) {
                        case MACH_SEND_TIMED_OUT:
                                 jack_error("MACH_SEND_TIMED_OUT %s\n",
				            client->control->name);
                                 break;
                                 
                           case MACH_RCV_TIMED_OUT:
                                 jack_error("MACH_RCV_TIMED_OUT %s\n",
				            client->control->name);
                                 break;
                     
                         case MACH_SEND_INVALID_DEST:
                                 jack_error("MACH_SEND_INVALID_DEST %s\n",
				            client->control->name);
                                 break;
                    }
                    */
                    
                    jack_error("jack_client_resume: send error for %s\n",
			       mach_error_string(err));
                    return err;
            }
        }
        
        return 0;
}

static inline int 
jack_client_suspend(jack_client_t * client)
{
        int err = 0;
        mach_msg_header_t * head = &client->message.header;
     
        head->msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
					 MACH_MSG_TYPE_MAKE_SEND_ONCE);
        head->msgh_remote_port = client->serverport;
	head->msgh_local_port = client->replyport;
	head->msgh_size = sizeof(mach_msg_header_t);
     
        err = mach_msg(head, MACH_SEND_MSG|MACH_RCV_MSG|MACH_SEND_TIMEOUT, 
		       sizeof(mach_msg_header_t), sizeof(client->message),
		       client->replyport, WAIT, MACH_PORT_NULL);
            
        if (err) {
            jack_error("jack_client_suspend: RPC error: %s\n",
		       mach_error_string(err));
            return -1;
        }
        
        return 0;
}

static inline void 
allocate_mach_serverport(jack_engine_t * engine, jack_client_internal_t *client)
{
        char buf[256];
        snprintf(buf, 256, "JackMachPort_%d", engine->portnum); 
        
        if (mach_port_allocate(engine->servertask, MACH_PORT_RIGHT_RECEIVE,
			       &client->serverport)){
            jack_error("allocate_mach_serverport: can't allocate mach port");
        }
        
        if (mach_port_insert_right(engine->servertask, client->serverport,
				   client->serverport, MACH_MSG_TYPE_MAKE_SEND)){
            jack_error("allocate_mach_serverport: error inserting mach rights");
        }
        
	if (bootstrap_register(engine->bp, buf, client->serverport)){
            jack_error("allocate_mach_serverport: can't check in mach port");
        }
        
        client->portnum = engine->portnum;
        engine->portnum++;
}

static inline int 
allocate_mach_clientport(jack_client_t * client, int portnum)
{
        char buf[256];
        snprintf(buf, 256, "JackMachPort_%d", portnum); 
        
        if (bootstrap_look_up(client->bp, buf, &client->serverport)){
            jack_error ("allocate_mach_clientport: can't find mach server port");
            return -1;
  	}
        
        if (mach_port_allocate(client->clienttask, MACH_PORT_RIGHT_RECEIVE,
			       &client->replyport)){
            jack_error("allocate_mach_clientport: can't allocate mach port");
            return -1;
        }
        
        return 0;
}

#endif /* __ipc__ */
