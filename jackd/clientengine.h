/*
 *  Client creation and destruction interfaces for JACK engine.
 *
 *  Copyright (C) 2001-2003 Paul Davis
 *  Copyright (C) 2004 Jack O'Quin
 *  
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

static inline int 
jack_client_is_internal (jack_client_internal_t *client)
{
	return (client->control->type == ClientInternal) ||
		(client->control->type == ClientDriver);
}

static inline char *
jack_client_state_name (jack_client_internal_t *client)
{
	static char *client_state_names[] = {
		"Not triggered",
		"Triggered",
		"Running",
		"Finished"
	};

	return client_state_names[client->control->state];
}

#define JACK_ERROR_WITH_SOCKETS 10000000

int	jack_client_activate (jack_engine_t *engine, jack_client_id_t id);
int	jack_client_deactivate (jack_engine_t *engine, jack_client_id_t id);
int	jack_client_create (jack_engine_t *engine, int client_fd);
void	jack_client_delete (jack_engine_t *engine,
			    jack_client_internal_t *client);
int	jack_mark_client_socket_error (jack_engine_t *engine, int fd);
jack_client_internal_t *
	jack_create_driver_client (jack_engine_t *engine, char *name);
void	jack_intclient_handle_request (jack_engine_t *engine,
				       jack_request_t *req);
void	jack_intclient_load_request (jack_engine_t *engine,
				     jack_request_t *req);
void	jack_intclient_name_request (jack_engine_t *engine,
				     jack_request_t *req);
void	jack_intclient_unload_request (jack_engine_t *engine,
				       jack_request_t *req);
int	jack_check_clients (jack_engine_t* engine, int with_timeout_check);
void	jack_remove_clients (jack_engine_t* engine, int* exit_freewheeling);
void    jack_client_registration_notify (jack_engine_t *engine,
					 const char* name, int yn);
void jack_remove_client (jack_engine_t *engine, jack_client_internal_t *client);
