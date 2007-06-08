/* -*- mode: c; c-file-style: "bsd"; -*- */
/*
    Copyright (C) 2001-2003 Paul Davis
    Copyright (C) 2005 Jussi Laako
    
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

*/

#include <config.h>

#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <limits.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <regex.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>

#include <jack/internal.h>
#include <jack/jack.h>
#include <jack/engine.h>
#include <jack/pool.h>
#include <jack/jslist.h>
#include <jack/version.h>
#include <jack/shm.h>
#include <jack/unlock.h>
#include <jack/thread.h>
#include <jack/varargs.h>
#include <jack/intsimd.h>

#include <sysdeps/time.h>

#include "local.h"

#include <sysdeps/poll.h>
#include <sysdeps/ipc.h>
#include <sysdeps/cycles.h>

#ifdef JACK_USE_MACH_THREADS
#include <sysdeps/pThreadUtilities.h>
#endif

static pthread_mutex_t client_lock;
static pthread_cond_t  client_ready;
#ifdef ARCH_X86
int cpu_type = 0;
#endif /* ARCH_X86 */

#define EVENT_POLL_INDEX 0
#define WAIT_POLL_INDEX 1
#define event_fd pollfd[EVENT_POLL_INDEX].fd
#define graph_wait_fd pollfd[WAIT_POLL_INDEX].fd

typedef struct {
    int status;
    struct _jack_client *client;
    const char *client_name;
} client_info;

#ifdef USE_DYNSIMD

#ifdef ARCH_X86

static int
have_3dnow ()
{
	unsigned int res = 0;

#ifdef __x86_64__
	asm volatile ("pushq %%rbx\n\t" : : : "memory");
#else
	asm volatile ("pushl %%ebx\n\t" : : : "memory");
#endif
	asm volatile (
		"movl $0x80000000, %%eax\n\t" \
		"cpuid\n\t" \
		"cmpl $0x80000001, %%eax\n\t" \
		"jl tdnow_prexit\n\t" \
		\
		"movl $0x80000001, %%eax\n\t" \
		"cpuid\n\t" \
		\
		"xorl %%eax, %%eax\n\t" \
		\
		"movl $1, %%ecx\n\t" \
		"shll $31, %%ecx\n\t" \
		"testl %%ecx, %%edx\n\t" \
		"jz tdnow_testexit\n\t" \
		"movl $1, %%eax\n\t" \
		\
		"movl $1, %%ecx\n\t" \
		"shll $30, %%ecx\n\t" \
		"testl %%ecx, %%edx\n\t" \
		"jz tdnow_testexit\n\t" \
		"movl $2, %%eax\n\t" \
		"jmp tdnow_testexit\n\t" \
		\
		"tdnow_prexit:\n\t" \
		"xorl %%eax, %%eax\n\t" \
		"tdnow_testexit:\n\t"
		: "=a" (res)
		:
		: "ecx", "edx", "memory");
#ifdef __x86_64__
	asm volatile ("popq %%rbx\n\t" : : : "memory");
#else
	asm volatile ("popl %%ebx\n\t" : : : "memory");
#endif
	return res;
}

static int
have_sse ()
{
	unsigned int res = 0;

#ifdef __x86_64__
	asm volatile ("pushq %%rbx\n\t" : : : "memory");
#else
	asm volatile ("pushl %%ebx\n\t" : : : "memory");
#endif
	asm volatile (
		"movl $1, %%eax\n\t" \
		"cpuid\n\t" \
		\
		"xorl %%eax, %%eax\n\t" \
		\
		"movl $1, %%ebx\n\t" \
		"shll $25, %%ebx\n\t" \
		"testl %%ebx, %%edx\n\t" \
		"jz sse_testexit\n\t" \
		"movl $1, %%eax\n\t" \
		\
		"movl $1, %%ebx\n\t" \
		"shll $26, %%ebx\n\t" \
		"testl %%ebx, %%edx\n\t" \
		"jz sse_testexit\n\t" \
		"movl $2, %%eax\n\t" \
		\
		"movl $1, %%ebx\n\t" \
		"testl %%ebx, %%ecx\n\t" \
		"jz sse_testexit\n\t" \
		"movl $3, %%eax\n\t" \
		\
		"sse_testexit:\n\t"
		: "=a" (res)
		:
		: "ecx", "edx", "memory");
#ifdef __x86_64__
	asm volatile ("popq %%rbx\n\t" : : : "memory");
#else
	asm volatile ("popl %%ebx\n\t" : : : "memory");
#endif
	return res;
}

static void
init_cpu ()
{
	cpu_type = ((have_3dnow() << 8) | have_sse());
	if (ARCH_X86_HAVE_3DNOW(cpu_type))
		fprintf(stderr, "Enhanced3DNow! detected\n");
	if (ARCH_X86_HAVE_SSE2(cpu_type))
		fprintf(stderr, "SSE2 detected\n");
	if ((!ARCH_X86_HAVE_3DNOW(cpu_type)) && (!ARCH_X86_HAVE_SSE2(cpu_type)))
		fprintf(stderr,
			"No supported SIMD instruction sets detected\n");
	jack_port_set_funcs();
}

#else /* ARCH_X86 */

static void
init_cpu ()
{
	jack_port_set_funcs();
}

#endif /* ARCH_X86 */

#endif /* USE_DYNSIMD */

char *jack_tmpdir = DEFAULT_TMP_DIR;

static int
jack_get_tmpdir ()
{
	FILE* in;
	size_t len;
	char buf[PATH_MAX+2]; /* allow tmpdir to live anywhere, plus newline, plus null */

	if ((in = popen ("jackd -l", "r")) == NULL) {
		return -1;
	}

	if (fgets (buf, sizeof (buf), in) == NULL) {
		fclose (in);
		return -1;
	}

	len = strlen (buf);

	if (buf[len-1] != '\n') {
		/* didn't get a whole line */
		fclose (in);
		return -1;
	}

	jack_tmpdir = (char *) malloc (len);
	memcpy (jack_tmpdir, buf, len-1);
	jack_tmpdir[len-1] = '\0';
	
	fclose (in);
	return 0;
}

void 
jack_error (const char *fmt, ...)
{
	va_list ap;
	char buffer[300];

	va_start (ap, fmt);
	vsnprintf (buffer, sizeof(buffer), fmt, ap);
	jack_error_callback (buffer);
	va_end (ap);
}

void 
default_jack_error_callback (const char *desc)
{
	fprintf(stderr, "%s\n", desc);
}

void 
silent_jack_error_callback (const char *desc)
{
}

void (*jack_error_callback)(const char *desc) = &default_jack_error_callback;

static int
oop_client_deliver_request (void *ptr, jack_request_t *req)
{
	int wok, rok;
	jack_client_t *client = (jack_client_t*) ptr;

	wok = (write (client->request_fd, req, sizeof (*req))
	       == sizeof (*req));
	rok = (read (client->request_fd, req, sizeof (*req))
	       == sizeof (*req));

	if (wok && rok) {		/* everything OK? */
		return req->status;
	}

	req->status = -1;		/* request failed */

	/* check for server shutdown */
	if (client->engine->engine_ok == 0)
		return req->status;

	/* otherwise report errors */
	if (!wok)
		jack_error ("cannot send request type %d to server",
				    req->type);
	if (!rok)
		jack_error ("cannot read result for request type %d from"
			    " server (%s)", req->type, strerror (errno));
	return req->status;
}

int
jack_client_deliver_request (const jack_client_t *client, jack_request_t *req)
{
	/* indirect through the function pointer that was set either
	 * by jack_client_open() or by jack_new_client_request() in
	 * the server.
	 */

	return client->control->deliver_request (client->control->deliver_arg,
						 req);
}

#if JACK_USE_MACH_THREADS 

jack_client_t *
jack_client_alloc ()
{
	jack_client_t *client;

	client = (jack_client_t *) malloc (sizeof (jack_client_t));
	client->pollfd = (struct pollfd *) malloc (sizeof (struct pollfd) * 1);
	client->pollmax = 1;
	client->request_fd = -1;
	client->event_fd = -1;
	client->upstream_is_jackd = 0;
	client->graph_wait_fd = -1;
	client->graph_next_fd = -1;
	client->ports = NULL;
	client->ports_ext = NULL;
	client->engine = NULL;
	client->control = NULL;
	client->thread_ok = FALSE;
	client->rt_thread_ok = FALSE;
	client->first_active = TRUE;
	client->on_shutdown = NULL;
	client->n_port_types = 0;
	client->port_segment = NULL;
	return client;
}

#else

jack_client_t *
jack_client_alloc ()
{
	jack_client_t *client;

	client = (jack_client_t *) malloc (sizeof (jack_client_t));
	client->pollfd = (struct pollfd *) malloc (sizeof (struct pollfd) * 2);
	client->pollmax = 2;
	client->request_fd = -1;
	client->event_fd = -1;
	client->upstream_is_jackd = 0;
	client->graph_wait_fd = -1;
	client->graph_next_fd = -1;
	client->ports = NULL;
	client->ports_ext = NULL;
	client->engine = NULL;
	client->control = NULL;
	client->thread_ok = FALSE;
	client->first_active = TRUE;
	client->on_shutdown = NULL;
	client->n_port_types = 0;
	client->port_segment = NULL;

#ifdef USE_DYNSIMD
	init_cpu();
#endif /* USE_DYNSIMD */

	return client;
}

#endif

/*
 * Build the jack_client_t structure for an internal client.
 */
jack_client_t *
jack_client_alloc_internal (jack_client_control_t *cc, jack_engine_t* engine)
{
	jack_client_t* client;

	client = jack_client_alloc ();

	client->control = cc;
	client->engine = engine->control;
	
	client->n_port_types = client->engine->n_port_types;
	client->port_segment = &engine->port_segment[0];

	return client;
}

static void
jack_client_free (jack_client_t *client)
{
	if (client->pollfd) {
		free (client->pollfd);
	}

	free (client);
}

void
jack_client_invalidate_port_buffers (jack_client_t *client)
{
	JSList *node;
	jack_port_t *port;

	/* This releases all local memory owned by input ports
	   and sets the buffer pointer to NULL. This will cause
	   jack_port_get_buffer() to reallocate space for the
	   buffer on the next call (if there is one).
	*/

	for (node = client->ports; node; node = jack_slist_next (node)) {
		port = (jack_port_t *) node->data;

		if (port->shared->flags & JackPortIsInput) {
			if (port->mix_buffer) {
				jack_pool_release (port->mix_buffer);
				port->mix_buffer = NULL;
			}
		}
	}
}

int
jack_client_handle_port_connection (jack_client_t *client, jack_event_t *event)
{
	jack_port_t *control_port;
	jack_port_t *other = 0;
	JSList *node;
	int need_free = FALSE;

	switch (event->type) {
	case PortConnected:
		other = jack_port_new (client, event->y.other_id,
				       client->engine);
		/* jack_port_by_id_int() always returns an internal
		 * port that does not need to be deallocated */
		control_port = jack_port_by_id_int (client, event->x.self_id,
						    &need_free);
		pthread_mutex_lock (&control_port->connection_lock);
		control_port->connections =
			jack_slist_prepend (control_port->connections,
					    (void *) other);
		pthread_mutex_unlock (&control_port->connection_lock);
		
		if (client->control->port_connect) {
			client->control->port_connect (control_port, other, 1, client->control->port_connect_arg);
		}

		break;

	case PortDisconnected:
		/* jack_port_by_id_int() always returns an internal
		 * port that does not need to be deallocated */
		control_port = jack_port_by_id_int (client, event->x.self_id,
						    &need_free);
		pthread_mutex_lock (&control_port->connection_lock);

		for (node = control_port->connections; node;
		     node = jack_slist_next (node)) {

			other = (jack_port_t *) node->data;

			if (other->shared->id == event->y.other_id) {
				control_port->connections =
					jack_slist_remove_link (
						control_port->connections,
						node);
				break;
			}
		}

		pthread_mutex_unlock (&control_port->connection_lock);

		if (node) {
			if (client->control->port_connect) {
				client->control->port_connect (control_port, other, 0, client->control->port_connect_arg);
			}
			jack_slist_free_1 (node);
			free (other);
		}

		break;

	default:
		/* impossible */
		break;
	}

	return 0;
}

#if JACK_USE_MACH_THREADS

static int 
jack_handle_reorder (jack_client_t *client, jack_event_t *event)
{	
	client->pollmax = 1;

	/* If the client registered its own callback for graph order events,
	   execute it now.
	*/

	if (client->control->graph_order) {
		client->control->graph_order (client->control->graph_order_arg);
	}

	return 0;
}

#else

static int 
jack_handle_reorder (jack_client_t *client, jack_event_t *event)
{	
	char path[PATH_MAX+1];

	if (client->graph_wait_fd >= 0) {
		DEBUG ("closing graph_wait_fd==%d", client->graph_wait_fd);
		close (client->graph_wait_fd);
		client->graph_wait_fd = -1;
	} 

	if (client->graph_next_fd >= 0) {
		DEBUG ("closing graph_next_fd==%d", client->graph_next_fd);
		close (client->graph_next_fd);
		client->graph_next_fd = -1;
	}

	sprintf (path, "%s-%" PRIu32, client->fifo_prefix, event->x.n);
	
	if ((client->graph_wait_fd = open (path, O_RDONLY|O_NONBLOCK)) < 0) {
		jack_error ("cannot open specified fifo [%s] for reading (%s)",
			    path, strerror (errno));
		return -1;
	}
	DEBUG ("opened new graph_wait_fd %d (%s)", client->graph_wait_fd, path);

	sprintf (path, "%s-%" PRIu32, client->fifo_prefix, event->x.n+1);
	
	if ((client->graph_next_fd = open (path, O_WRONLY|O_NONBLOCK)) < 0) {
		jack_error ("cannot open specified fifo [%s] for writing (%s)",
			    path, strerror (errno));
		return -1;
	}

	client->upstream_is_jackd = event->y.n;
	client->pollmax = 2;

	DEBUG ("opened new graph_next_fd %d (%s) (upstream is jackd? %d)",
	       client->graph_next_fd, path, 
	       client->upstream_is_jackd);

	/* If the client registered its own callback for graph order events,
	   execute it now.
	*/

	if (client->control->graph_order) {
		client->control->graph_order (client->control->graph_order_arg);
	}

	return 0;
}

#endif
		
static int
server_connect (const char *server_name)
{
	int fd;
	struct sockaddr_un addr;
	int which = 0;

        char server_dir[PATH_MAX+1] = "";

	if ((fd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0) {
		jack_error ("cannot create client socket (%s)",
			    strerror (errno));
		return -1;
	}

	//JOQ: temporary debug message
	//fprintf (stderr, "DEBUG: connecting to `%s' server\n", server_name);

	addr.sun_family = AF_UNIX;
	snprintf (addr.sun_path, sizeof (addr.sun_path) - 1, "%s/jack_%d",
                  jack_server_dir (server_name, server_dir) , which);

	if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) < 0) {
		close (fd);
		return -1;
	}

	return fd;
}

static int
server_event_connect (jack_client_t *client, const char *server_name)
{
	int fd;
	struct sockaddr_un addr;
	jack_client_connect_ack_request_t req;
	jack_client_connect_ack_result_t res;

        char server_dir[PATH_MAX+1] = "";

	if ((fd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0) {
		jack_error ("cannot create client event socket (%s)",
			    strerror (errno));
		return -1;
	}

	addr.sun_family = AF_UNIX;
	snprintf (addr.sun_path, sizeof (addr.sun_path) - 1, "%s/jack_ack_0",
		  jack_server_dir (server_name,server_dir));

	if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) < 0) {
		jack_error ("cannot connect to jack server for events",
			    strerror (errno));
		close (fd);
		return -1;
	}

	req.client_id = client->control->id;

	if (write (fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot write event connect request to server (%s)",
			    strerror (errno));
		close (fd);
		return -1;
	}

	if (read (fd, &res, sizeof (res)) != sizeof (res)) {
		jack_error ("cannot read event connect result from server (%s)",
			    strerror (errno));
		close (fd);
		return -1;
	}

	if (res.status != 0) {
		jack_error ("cannot connect to server for event stream (%s)",
			    strerror (errno));
		close (fd);
		return -1;
	}

	return fd;
}

/* Exec the JACK server in this process.  Does not return. */
static void
_start_server (const char *server_name)
{
	FILE* fp = 0;
	char filename[255];
	char arguments[255];
	char buffer[255];
	char* command = 0;
	size_t pos = 0;
	size_t result = 0;
	char** argv = 0;
	int i = 0;
	int good = 0;
	int ret;
	
	snprintf(filename, 255, "%s/.jackdrc", getenv("HOME"));
	fp = fopen(filename, "r");

	if (!fp) {
		fp = fopen("/etc/jackdrc", "r");
	}
	/* if still not found, check old config name for backwards compatability */
	if (!fp) {
		fp = fopen("/etc/jackd.conf", "r");
	}

	if (fp) {
		arguments[0] = '\0';
		ret = fscanf(fp, "%s", buffer);
		while(ret != 0 && ret != EOF) {
			strcat(arguments, buffer);
			strcat(arguments, " ");
			ret = fscanf(fp, "%s", buffer);
		}
		if (strlen(arguments) > 0) {
			good = 1;
		}
	}

	if (!good) {
#if defined(USE_CAPABILITIES)
		command = JACK_LOCATION "/jackstart";
		strncpy(arguments, JACK_LOCATION "/jackstart -T -R -d "
			JACK_DEFAULT_DRIVER " -p 512", 255);
#else /* !USE_CAPABILITIES */
		command = JACK_LOCATION "/jackd";
		strncpy(arguments, JACK_LOCATION "/jackd -T -d "
			JACK_DEFAULT_DRIVER, 255);
#endif /* USE_CAPABILITIES */
	} else {
		result = strcspn(arguments, " ");
		command = (char *) malloc(result+1);
		strncpy(command, arguments, result);
		command[result] = '\0';
	}

	argv = (char **) malloc (255);
  
	while(1) {
		/* insert -T and -nserver_name in front of arguments */
		if (i == 1) {
			argv[i] = (char *) malloc(strlen ("-T") + 1);
			strcpy (argv[i++], "-T"); 
			if (server_name) {
				size_t optlen = strlen ("-n");
				char *buf =
					malloc (optlen
						+ strlen (server_name) + 1);
				strcpy (buf, "-n");
				strcpy (buf+optlen, server_name);
				argv[i++] = buf;
			}
		}

		result = strcspn(arguments+pos, " ");
		if (result == 0) {
			break;
		}
		argv[i] = (char*)malloc(result+1);

		strncpy(argv[i], arguments+pos, result);
		argv[i][result] = '\0';

		pos += result+1;
		++i;
	}
	argv[i] = 0;
	execv (command, argv);

	/* If execv() succeeds, it does not return.  There's no point
	 * in calling jack_error() here in the child process. */
	fprintf (stderr, "exec of JACK server (command = \"%s\") failed: %s\n", command, strerror (errno));
}

int
start_server (const char *server_name, jack_options_t options)
{
	if ((options & JackNoStartServer)
	    || getenv("JACK_NO_START_SERVER")) {
		return 1;
	}

	/* The double fork() forces the server to become a child of
	 * init, which will always clean up zombie process state on
	 * termination.  This even works in cases where the server
	 * terminates but this client does not.
	 *
	 * Since fork() is usually implemented using copy-on-write
	 * virtual memory tricks, the overhead of the second fork() is
	 * probably relatively small.
	 */
	switch (fork()) {
	case 0:				/* child process */
		switch (fork()) {
		case 0:			/* grandchild process */
			_start_server(server_name);
			_exit (99);	/* exec failed */
		case -1:
			_exit (98);
		default:
			_exit (0);
		}
	case -1:			/* fork() error */
		return 1;		/* failed to start server */
	}

	/* only the original parent process goes here */
	return 0;			/* (probably) successful */
}

static int
jack_request_client (ClientType type,
		     const char* client_name, jack_options_t options,
		     jack_status_t *status, jack_varargs_t *va,
		     jack_client_connect_result_t *res, int *req_fd)
{
	jack_client_connect_request_t req;

	*req_fd = -1;
	memset (&req, 0, sizeof (req));
	req.options = options;

	if (strlen (client_name) >= sizeof (req.name)) {
		jack_error ("\"%s\" is too long to be used as a JACK client"
			    " name.\n"
			    "Please use %lu characters or less.",
			    client_name, sizeof (req.name));
		return -1;
	}

	if (va->load_name
	    && (strlen (va->load_name) > sizeof (req.object_path) - 1)) {
		jack_error ("\"%s\" is too long to be used as a JACK shared"
			    " object name.\n"
			     "Please use %lu characters or less.",
			    va->load_name, sizeof (req.object_path) - 1);
		return -1;
	}

	if (va->load_init
	    && (strlen (va->load_init) > sizeof (req.object_data) - 1)) {
		jack_error ("\"%s\" is too long to be used as a JACK shared"
			    " object data string.\n"
			     "Please use %lu characters or less.",
			    va->load_init, sizeof (req.object_data) - 1);
		return -1;
	}

	if ((*req_fd = server_connect (va->server_name)) < 0) {
		int trys;
		if (start_server(va->server_name, options)) {
			*status |= (JackFailure|JackServerFailed);
			goto fail;
		}
		trys = 5;
		do {
			sleep(1);
			if (--trys < 0) {
				*status |= (JackFailure|JackServerFailed);
				goto fail;
			}
		} while ((*req_fd = server_connect (va->server_name)) < 0);
		*status |= JackServerStarted;
	}

	/* format connection request */
	req.protocol_v = jack_protocol_version;
	req.load = TRUE;
	req.type = type;
	snprintf (req.name, sizeof (req.name),
		  "%s", client_name);
	snprintf (req.object_path, sizeof (req.object_path),
		  "%s", va->load_name);
	snprintf (req.object_data, sizeof (req.object_data),
		  "%s", va->load_init);

	if (write (*req_fd, &req, sizeof (req)) != sizeof (req)) {
		jack_error ("cannot send request to jack server (%s)",
			    strerror (errno));
		*status |= (JackFailure|JackServerError);
		goto fail;
	}

	if (read (*req_fd, res, sizeof (*res)) != sizeof (*res)) {

		if (errno == 0) {
			/* server shut the socket */
			jack_error ("could not attach as client");
			*status |= (JackFailure|JackServerError);
			goto fail;
		}
		
		if (errno == ECONNRESET) {
			jack_error ("could not attach as JACK client "
				    "(server has exited)");
			*status |= (JackFailure|JackServerError);
			goto fail;
		}
		
		jack_error ("cannot read response from jack server (%s)",
			    strerror (errno));
		*status |= (JackFailure|JackServerError);
		goto fail;
	}

	*status |= res->status;		/* return server status bits */

	if (*status & JackFailure) {
		if (*status & JackVersionError) {
			jack_error ("client linked with incompatible libjack"
				    " version.");
		}
		jack_error ("could not attach to JACK server");
		*status |= JackServerError;
		goto fail;
	}

	switch (type) {
	case ClientDriver:
	case ClientInternal:
		close (*req_fd);
		*req_fd = -1;
		break;

	default:
		break;
	}

	return 0;

  fail:
	if (*req_fd >= 0) {
		close (*req_fd);
		*req_fd = -1;
	}
	return -1;
}

int
jack_attach_port_segment (jack_client_t *client, jack_port_type_id_t ptid)
{
	/* Lookup, attach and register the port/buffer segments in use
	 * right now. 
	 */

	if (client->control->type != ClientExternal) {
		jack_error("Only external clients need attach port segments");
		abort();
	}

	/* make sure we have space to store the port
	   segment information.
	*/

	if (ptid >= client->n_port_types) {
		
		client->port_segment = (jack_shm_info_t*)
			realloc (client->port_segment,
				 sizeof (jack_shm_info_t) * (ptid+1));

		memset (&client->port_segment[client->n_port_types],
			0,
			sizeof (jack_shm_info_t) * 
			(ptid - client->n_port_types));
		
		client->n_port_types = ptid + 1;

	} else {

		/* release any previous segment */
		jack_release_shm (&client->port_segment[ptid]);
	}

	/* get the index into the shm registry */

	client->port_segment[ptid].index =
		client->engine->port_types[ptid].shm_registry_index;

	/* attach the relevant segment */

	if (jack_attach_shm (&client->port_segment[ptid])) {
		jack_error ("cannot attach port segment shared memory"
			    " (%s)", strerror (errno));
		return -1;
	}

	return 0;
}

jack_client_t *
jack_client_open (const char *client_name,
		  jack_options_t options,
		  jack_status_t *status, ...)
{
	/* optional arguments: */
	va_list ap;			/* variable argument pointer */
	jack_varargs_t va;		/* variable arguments */

	int req_fd = -1;
	int ev_fd = -1;
	jack_client_connect_result_t  res;
	jack_client_t *client;
	jack_port_type_id_t ptid;
	jack_status_t my_status;

	if (status == NULL)		/* no status from caller? */
		status = &my_status;	/* use local status word */
	*status = 0;

	/* validate parameters */
	if ((options & ~JackOpenOptions)) {
		*status |= (JackFailure|JackInvalidOption);
		return NULL;
	}

	/* parse variable arguments */
	va_start (ap, status);
	jack_varargs_parse (options, ap, &va);
	va_end (ap);

	/* External clients need to know where the tmpdir used for
	   communication with the server lives
	*/
	if (jack_get_tmpdir ()) {
		*status |= JackFailure;
		return NULL;
	}

	/* External clients need this initialized.  It is already set
	 * up in the server's address space for internal clients.
	 */
	jack_init_time ();

	if (jack_request_client (ClientExternal, client_name, options, status,
				 &va, &res, &req_fd)) {
		return NULL;
	}

	/* Allocate the jack_client_t structure in local memory.
	 * Shared memory is not accessible yet. */
	client = jack_client_alloc ();
	strcpy (client->name, res.name);
	strcpy (client->fifo_prefix, res.fifo_prefix);
	client->request_fd = req_fd;
	client->pollfd[EVENT_POLL_INDEX].events =
		POLLIN|POLLERR|POLLHUP|POLLNVAL;
#ifndef JACK_USE_MACH_THREADS
	client->pollfd[WAIT_POLL_INDEX].events =
		POLLIN|POLLERR|POLLHUP|POLLNVAL;
#endif

	/* Don't access shared memory until server connected. */
	if (jack_initialize_shm (va.server_name)) {
		jack_error ("Unable to initialize shared memory.");
		*status |= (JackFailure|JackShmFailure);
		goto fail;
	}

	/* attach the engine control/info block */
	client->engine_shm = res.engine_shm;
	if (jack_attach_shm (&client->engine_shm)) {
		jack_error ("cannot attached engine control shared memory"
			    " segment");
		goto fail;
	}
	
	client->engine = (jack_control_t *) jack_shm_addr (&client->engine_shm);

	/* initialize clock source as early as possible */
	jack_set_clock_source (client->engine->clock_source);

	/* now attach the client control block */
	client->control_shm = res.client_shm;
	if (jack_attach_shm (&client->control_shm)) {
		jack_error ("cannot attached client control shared memory"
			    " segment");
		goto fail;
	}
	
	client->control = (jack_client_control_t *)
		jack_shm_addr (&client->control_shm);

	/* Nobody else needs to access this shared memory any more, so
	 * destroy it.  Because we have it attached, it won't vanish
	 * till we exit (and release it).
	 */
	jack_destroy_shm (&client->control_shm);

	client->n_port_types = client->engine->n_port_types;
	client->port_segment = (jack_shm_info_t *)
		malloc (sizeof (jack_shm_info_t) * client->n_port_types);
	
	for (ptid = 0; ptid < client->n_port_types; ++ptid) {
		client->port_segment[ptid].index =
			client->engine->port_types[ptid].shm_registry_index;
		client->port_segment[ptid].attached_at = MAP_FAILED;
		jack_attach_port_segment (client, ptid);
	}

	/* set up the client so that it does the right thing for an
	 * external client 
	 */
	client->control->deliver_request = oop_client_deliver_request;
	client->control->deliver_arg = client;

	if ((ev_fd = server_event_connect (client, va.server_name)) < 0) {
		goto fail;
	}

	client->event_fd = ev_fd;
        
#ifdef JACK_USE_MACH_THREADS
        /* specific resources for server/client real-time thread
	 * communication */
	client->clienttask = mach_task_self();
        
	if (task_get_bootstrap_port(client->clienttask, &client->bp)){
            jack_error ("Can't find bootstrap port");
            goto fail;
        }
        
        if (allocate_mach_clientport(client, res.portnum) < 0) {
            jack_error("Can't allocate mach port");
            goto fail; 
        }; 
#endif /* JACK_USE_MACH_THREADS */
 	return client;
	
  fail:
	if (client->engine) {
		jack_release_shm (&client->engine_shm);
		client->engine = 0;
	}
	if (client->control) {
		jack_release_shm (&client->control_shm);
		client->control = 0;
	}
	if (req_fd >= 0) {
		close (req_fd);
	}
	if (ev_fd >= 0) {
		close (ev_fd);
	}
	free (client);

	return NULL;
}

jack_client_t *
jack_client_new (const char *client_name)
{
	jack_options_t options = JackUseExactName;
	if (getenv("JACK_START_SERVER") == NULL)
		options |= JackNoStartServer;

	return jack_client_open (client_name, options, NULL);
}

char *
jack_get_client_name (jack_client_t *client)
{
	return client->name;
}

int
jack_internal_client_new (const char *client_name,
			  const char *so_name, const char *so_data)
{
	jack_client_connect_result_t res;
	int req_fd;
	jack_varargs_t va;
	jack_status_t status;
	jack_options_t options = JackUseExactName;

	if (getenv("JACK_START_SERVER") == NULL)
		options |= JackNoStartServer;

	jack_varargs_init (&va);
	va.load_name = (char *) so_name;
	va.load_init = (char *) so_data;

	return jack_request_client (ClientInternal, client_name,
				    options, &status, &va, &res, &req_fd);
}

char *
jack_default_server_name (void)
{
	char *server_name;
	if ((server_name = getenv("JACK_DEFAULT_SERVER")) == NULL)
		server_name = "default";
	return server_name;
}

/* returns the name of the per-user subdirectory of jack_tmpdir */
char *
jack_user_dir (void)
{
	static char user_dir[PATH_MAX+1] = "";

	/* format the path name on the first call */
	if (user_dir[0] == '\0') {
		if (getenv ("JACK_PROMISCUOUS_SERVER")) {
			snprintf (user_dir, sizeof (user_dir), "%s/jack",
				  jack_tmpdir);
		} else {
			snprintf (user_dir, sizeof (user_dir), "%s/jack-%d",
				  jack_tmpdir, getuid ());
		}
	}

	return user_dir;
}

/* returns the name of the per-server subdirectory of jack_user_dir() */
char *
jack_server_dir (const char *server_name, char *server_dir)
{
	/* format the path name into the suppled server_dir char array,
	 * assuming that server_dir is at least as large as PATH_MAX+1 */

	snprintf (server_dir, PATH_MAX+1, "%s/%s",
		  jack_user_dir (), server_name);

	return server_dir;
}

void
jack_internal_client_close (const char *client_name)
{
	jack_client_connect_request_t req;
	int fd;
	char *server_name = jack_default_server_name ();

	req.load = FALSE;
	snprintf (req.name, sizeof (req.name), "%s", client_name);
	
	if ((fd = server_connect (server_name)) < 0) {
		return;
	}

	if (write (fd, &req, sizeof (req)) != sizeof(req)) {
		jack_error ("cannot deliver ClientUnload request to JACK "
			    "server.");
	}
	
	/* no response to this request */
	
	close (fd);
	return;
}

int
jack_recompute_total_latencies (jack_client_t* client)
{
	jack_request_t request;

	request.type = RecomputeTotalLatencies;
	return jack_client_deliver_request (client, &request);
}

int
jack_recompute_total_latency (jack_client_t* client, jack_port_t* port)
{
	jack_request_t request;

	request.type = RecomputeTotalLatency;
	request.x.port_info.port_id = port->shared->id;
	return jack_client_deliver_request (client, &request);
}

int
jack_set_freewheel (jack_client_t* client, int onoff)
{
	jack_request_t request;

	request.type = onoff ? FreeWheel : StopFreeWheel;
	return jack_client_deliver_request (client, &request);
}

void
jack_start_freewheel (jack_client_t* client)
{
	jack_client_control_t *control = client->control;

	if (client->engine->real_time) {
#if JACK_USE_MACH_THREADS 
		jack_drop_real_time_scheduling (client->process_thread);
#else
		jack_drop_real_time_scheduling (client->thread);
#endif
	}

	if (control->freewheel_cb) {
		control->freewheel_cb (1, control->freewheel_arg);
	}
}

void
jack_stop_freewheel (jack_client_t* client)
{
	jack_client_control_t *control = client->control;

	if (client->engine->real_time) {
#if JACK_USE_MACH_THREADS 
		jack_acquire_real_time_scheduling (client->process_thread,
				client->engine->client_priority);
#else
		jack_acquire_real_time_scheduling (client->thread, 
				client->engine->client_priority);
#endif
	}

	if (control->freewheel_cb) {
		control->freewheel_cb (0, control->freewheel_arg);
	}
}

static void
jack_client_thread_suicide (jack_client_t* client)
{
	if (client->on_shutdown) {
		jack_error ("zombified - calling shutdown handler");
		client->on_shutdown (client->on_shutdown_arg);
	} else {
		jack_error ("jack_client_thread zombified - exiting from JACK");
		jack_client_close (client);
		/* Need a fix : possibly make client crash if
		 * zombified without shutdown handler 
		 */
	}

	pthread_exit (0);
	/*NOTREACHED*/
}

static int
jack_client_process_events (jack_client_t* client)
{
	jack_event_t event;
	char status = 0;
	jack_client_control_t *control = client->control;

	if (client->pollfd[EVENT_POLL_INDEX].revents & POLLIN) {
		
		DEBUG ("client receives an event, "
		       "now reading on event fd");
                
		/* server has sent us an event. process the
		 * event and reply */
		
		if (read (client->event_fd, &event, sizeof (event))
		    != sizeof (event)) {
			jack_error ("cannot read server event (%s)",
				    strerror (errno));
			return -1;
		}
		
		status = 0;
		
		switch (event.type) {
		case PortRegistered:
			if (control->port_register) {
				control->port_register
					(event.x.port_id, TRUE,
					 control->port_register_arg);
			} 
			break;
			
		case PortUnregistered:
			if (control->port_register) {
				control->port_register
					(event.x.port_id, FALSE,
					 control->port_register_arg);
			}
			break;
			
		case ClientRegistered:
			if (control->client_register) {
				control->client_register
					(event.x.name, TRUE,
					 control->client_register_arg);
			} 
			break;
			
		case ClientUnregistered:
			if (control->client_register) {
				control->client_register
					(event.x.name, FALSE,
					 control->client_register_arg);
			}
			break;
			
		case GraphReordered:
			status = jack_handle_reorder (client, &event);
			break;
			
		case PortConnected:
		case PortDisconnected:
			status = jack_client_handle_port_connection
				(client, &event);
			break;
			
		case BufferSizeChange:
			jack_client_invalidate_port_buffers (client);
			if (control->bufsize) {
				status = control->bufsize
					(control->nframes,
					 control->bufsize_arg);
			} 
			break;
			
		case SampleRateChange:
			if (control->srate) {
				status = control->srate
					(control->nframes,
					 control->srate_arg);
			}
			break;
			
		case XRun:
			if (control->xrun) {
				status = control->xrun
					(control->xrun_arg);
			}
			break;
			
		case AttachPortSegment:
			jack_attach_port_segment (client, event.y.ptid);
			break;
			
		case StartFreewheel:
			jack_start_freewheel (client);
			break;
			
		case StopFreewheel:
			jack_stop_freewheel (client);
			break;
		}
		
		DEBUG ("client has dealt with the event, writing "
		       "response on event fd");
		
		if (write (client->event_fd, &status, sizeof (status))
		    != sizeof (status)) {
			jack_error ("cannot send event response to "
				    "engine (%s)", strerror (errno));
			return -1;
		}
	}

	return 0;
}

static int
jack_client_core_wait (jack_client_t* client)
{
	jack_client_control_t *control = client->control;

	DEBUG ("client polling on %s", client->pollmax == 2 ? 
	       "event_fd and graph_wait_fd..." :
	       "event_fd only");
	
	while (1) {
		if (poll (client->pollfd, client->pollmax, 1000) < 0) {
			if (errno == EINTR) {
				continue;
			}
			jack_error ("poll failed in client (%s)",
				    strerror (errno));
			return -1;
		}

		pthread_testcancel();

#ifndef JACK_USE_MACH_THREADS 
		
		/* get an accurate timestamp on waking from poll for a
		 * process() cycle. 
		 */
		
		if (client->graph_wait_fd >= 0
		    && client->pollfd[WAIT_POLL_INDEX].revents & POLLIN) {
			control->awake_at = jack_get_microseconds();
		}
		
		DEBUG ("pfd[EVENT].revents = 0x%x pfd[WAIT].revents = 0x%x",
		       client->pollfd[EVENT_POLL_INDEX].revents,
		       client->pollfd[WAIT_POLL_INDEX].revents);
		
		if (client->graph_wait_fd >= 0 &&
		    (client->pollfd[WAIT_POLL_INDEX].revents & ~POLLIN)) {
			
			/* our upstream "wait" connection
			   closed, which either means that
			   an intermediate client exited, or
			   jackd exited, or jackd zombified
			   us.
			   
			   we can discover the zombification
			   via client->control->dead, but
			   the other two possibilities are
			   impossible to identify just from
			   this situation. so we have to
			   check what we are connected to,
			   and act accordingly.
			*/
			
			if (client->upstream_is_jackd) {
				DEBUG ("WE DIE\n");
				return 0;
			} else {
				DEBUG ("WE PUNT\n");
				/* don't poll on the wait fd
				 * again until we get a
				 * GraphReordered event.
				 */
				
				client->graph_wait_fd = -1;
				client->pollmax = 1;
			}
		}
#endif
		
		if (jack_client_process_events (client)) {
			DEBUG ("event processing failed\n");
			return 0;
		}
		
		if ((client->pollfd[WAIT_POLL_INDEX].revents & POLLIN)) {
			DEBUG ("time to run process()\n");
			break;
		}
	}

	if (client->control->dead || client->pollfd[EVENT_POLL_INDEX].revents & ~POLLIN) {
		DEBUG ("client appears dead or event pollfd has error status\n");
		return -1;
	}

	return 0;
}

static int
jack_wake_next_client (jack_client_t* client)
{
	char c = 0;

	if (write (client->graph_next_fd, &c, sizeof (c))
	    != sizeof (c)) {
		jack_error ("cannot continue execution of the "
			    "processing graph (%s)",
			    strerror(errno));
		return -1;
	}
	
	DEBUG ("client sent message to next stage by %" PRIu64
	       ", client reading on graph_wait_fd==%d", 
	       jack_get_microseconds(), client->graph_wait_fd);
	
	DEBUG("reading cleanup byte from pipe %d\n", client->graph_wait_fd);
	
	if ((read (client->graph_wait_fd, &c, sizeof (c))
	     != sizeof (c))) {
		jack_error ("cannot complete execution of the "
			    "processing graph (%s)",
			    strerror(errno));
		return -1;
	}
	
	return 0;
}

static jack_nframes_t 
jack_thread_first_wait (jack_client_t* client)
{
	if (jack_client_core_wait (client)) {
		return 0;
	}
	return client->control->nframes;
}
		
jack_nframes_t
jack_thread_wait (jack_client_t* client, int status)
{
	jack_client_control_t *control = client->control;

   /* SECTION ONE: HOUSEKEEPING/CLEANUP FROM LAST DATA PROCESSING */

	/* housekeeping/cleanup after data processing */

	if (status == 0 && control->timebase_cb) {
		jack_call_timebase_master (client);
	}
	
	/* end preemption checking */
	CHECK_PREEMPTION (client->engine, FALSE);
	
	control->finished_at = jack_get_microseconds();
	
	/* wake the next client in the chain (could be the server), 
	   and check if we were killed during the process
	   cycle.
	*/
	
	if (jack_wake_next_client (client)) {
		DEBUG("client cannot wake next, or is dead\n");
		return 0;
	}

	if (status || client->control->dead || !client->engine->engine_ok) {
		return 0;
	}
	
   /* SECTION TWO: WAIT FOR NEXT DATA PROCESSING TIME */

	if (jack_client_core_wait (client)) {
		return 0;
	}

   /* SECTION THREE: START NEXT DATA PROCESSING TIME */

	/* Time to do data processing */

	control->state = Running;
	
	/* begin preemption checking */
	CHECK_PREEMPTION (client->engine, TRUE);
	
	if (control->sync_cb)
		jack_call_sync_client (client);

	return control->nframes;
}

static void *
jack_client_thread (void *arg)
{
	jack_client_t *client = (jack_client_t *) arg;
	jack_client_control_t *control = client->control;

	pthread_mutex_lock (&client_lock);
	client->thread_ok = TRUE;
	client->thread_id = pthread_self();
	pthread_cond_signal (&client_ready);
	pthread_mutex_unlock (&client_lock);

	control->pid = getpid();
	control->pgrp = getpgrp();

	DEBUG ("client thread is now running");

	if (control->thread_init) {
		DEBUG ("calling client thread init callback");
		control->thread_init (control->thread_init_arg);
	}

	/* wait for first wakeup from server */

	if (jack_thread_first_wait (client) == control->nframes) {
		/* now run till we're done */
		if (control->process) {
			/* run process callback, then wait... ad-infinitum */
			while (jack_thread_wait (client, 
						 control->process (control->nframes, 
								   control->process_arg)) ==
			       control->nframes)
				;
		} else {
			/* no process handling but still need to process events */
			while (jack_thread_wait (client, 0) == control->nframes)
				;
		}
	}

	jack_client_thread_suicide (client);
	/*NOTREACHED*/
	return (void *) 0;
}

#ifdef JACK_USE_MACH_THREADS
/* real-time thread : separated from the normal client thread, it will
 * communicate with the server using fast mach RPC mechanism */

static void *
jack_client_process_thread (void *arg)
{
	jack_client_t *client = (jack_client_t *) arg;
	jack_client_control_t *control = client->control;
	int err = 0;
      
	if (client->control->thread_init) {
	/* this means that the init callback will be called twice -taybin*/
		DEBUG ("calling client thread init callback");
		client->control->thread_init (client->control->thread_init_arg);
	}

	client->control->pid = getpid();
	DEBUG ("client process thread is now running");
        
	client->rt_thread_ok = TRUE;
            
   	while (err == 0) {
	
		if (jack_client_suspend(client) < 0) {
				jack_error ("jack_client_process_thread :resume error");
				goto zombie;
		}
		
		control->awake_at = jack_get_microseconds();
		
		DEBUG ("client resumed");
		 
		control->state = Running;

		if (control->sync_cb)
			jack_call_sync_client (client);

		if (control->process) {
			if (control->process (control->nframes,
				control->process_arg) == 0) {
					control->state = Finished;
			}
		} else {
			control->state = Finished;
		}

		if (control->timebase_cb)
			jack_call_timebase_master (client);
                
		control->finished_at = jack_get_microseconds();
                
		DEBUG ("client finished processing at %Lu (elapsed = %f usecs)",
			control->finished_at,
			((float)(control->finished_at - control->awake_at)));
                  
		/* check if we were killed during the process cycle
		 * (or whatever) */

		if (client->control->dead) {
				jack_error ("jack_client_process_thread: "
						"client->control->dead");
				goto zombie;
		}

		DEBUG("process cycle fully complete\n");

	}
        
 	return (void *) ((intptr_t)err);

  zombie:
        
        jack_error ("jack_client_process_thread : zombified");
        
        client->rt_thread_ok = FALSE;

	if (client->on_shutdown) {
		jack_error ("zombified - calling shutdown handler");
		client->on_shutdown (client->on_shutdown_arg);
	} else {
		jack_error ("jack_client_process_thread zombified - exiting from JACK");
		/* Need a fix : possibly make client crash if
		 * zombified without shutdown handler */
		jack_client_close (client); 
	}

	pthread_exit (0);
	/*NOTREACHED*/
	return 0;
}
#endif /* JACK_USE_MACH_THREADS */

static int
jack_start_thread (jack_client_t *client)
{
 	if (client->engine->real_time) {

#ifdef USE_MLOCK
		if (client->engine->do_mlock
		    && (mlockall (MCL_CURRENT | MCL_FUTURE) != 0)) {
			jack_error ("cannot lock down memory for RT thread "
				    "(%s)", strerror (errno));
			 
#ifdef ENSURE_MLOCK
			 return -1;
#endif /* ENSURE_MLOCK */
		 }
		 
		 if (client->engine->do_munlock) {
			 cleanup_mlock ();
		 }
#endif /* USE_MLOCK */
	}

#ifdef JACK_USE_MACH_THREADS
/* Stephane Letz : letz@grame.fr
   On MacOSX, the normal thread does not need to be real-time.
*/
	if (jack_client_create_thread (client, 
				       &client->thread,
				       client->engine->client_priority,
				       FALSE,
				       jack_client_thread, client)) {
		return -1;
	}
#else
	if (jack_client_create_thread (client,
				&client->thread,
				client->engine->client_priority,
				client->engine->real_time,
				jack_client_thread, client)) {
		return -1;
	}

#endif

#ifdef JACK_USE_MACH_THREADS

	/* a secondary thread that runs the process callback and uses
	   ultra-fast Mach primitives for inter-thread signalling.

	   XXX in a properly structured JACK, there would be no
	   need for this, because we would have client wake up
	   methods that encapsulated the underlying mechanism
	   used.

	*/

	if (jack_client_create_thread(client,
				      &client->process_thread,
				      client->engine->client_priority,
				      client->engine->real_time,
				      jack_client_process_thread, client)) {
		return -1;
	}
#endif /* JACK_USE_MACH_THREADS */

	return 0;
}

int 
jack_activate (jack_client_t *client)
{
	jack_request_t req;

	/* we need to scribble on our stack to ensure that its memory
	 * pages are actually mapped (more important for mlockall(2)
	 * usage in jack_start_thread())
	 */
         
	char buf[JACK_THREAD_STACK_TOUCH];
	int i;

	for (i = 0; i < JACK_THREAD_STACK_TOUCH; i++) {
		buf[i] = (char) (i & 0xff);
	}

	if (client->control->type == ClientInternal ||
	    client->control->type == ClientDriver) {
		goto startit;
	}

	/* get the pid of the client process to pass it to engine */

	client->control->pid = getpid ();

#ifdef USE_CAPABILITIES

	if (client->engine->has_capabilities != 0 &&
	    client->control->pid != 0 && client->engine->real_time != 0) {

		/* we need to ask the engine for realtime capabilities
		   before trying to start the realtime thread
		*/

		req.type = SetClientCapabilities;
		req.x.client_id = client->control->id;
		req.x.cap_pid = client->control->pid;

		jack_client_deliver_request (client, &req);

		if (req.status) {

			/* what to do? engine is running realtime, it
			   is using capabilities and has them
			   (otherwise we would not get an error
			   return) but for some reason it could not
			   give the client the required capabilities.
			   For now, leave the client so that it
			   still runs, albeit non-realtime.
			*/
			
			jack_error ("could not receive realtime capabilities, "
				    "client will run non-realtime");
		} 
	}
#endif /* USE_CAPABILITIES */

	if (client->first_active) {

		pthread_mutex_init (&client_lock, NULL);
		pthread_cond_init (&client_ready, NULL);
		
		pthread_mutex_lock (&client_lock);

		if (jack_start_thread (client)) {
			pthread_mutex_unlock (&client_lock);
			return -1;
		}

		pthread_cond_wait (&client_ready, &client_lock);
		pthread_mutex_unlock (&client_lock);

		if (!client->thread_ok) {
			jack_error ("could not start client thread");
			return -1;
		}

		client->first_active = FALSE;
	}

  startit:

	req.type = ActivateClient;
	req.x.client_id = client->control->id;

	return jack_client_deliver_request (client, &req);
}

int 
jack_deactivate (jack_client_t *client)
{
	jack_request_t req;
	int rc = ESRCH;			/* already shut down */
	
	if (client && client->control) { /* not shut down? */
		rc = 0;
		if (client->control->active) { /* still active? */
			req.type = DeactivateClient;
			req.x.client_id = client->control->id;
			rc = jack_client_deliver_request (client, &req);
		}
	}
	return rc;
}

int
jack_client_close (jack_client_t *client)
{
	JSList *node;
	void *status;
	int rc;

	rc = jack_deactivate (client);
	if (rc == ESRCH) {		/* already shut down? */
		return rc;
	}

	if (client->control->type == ClientExternal) {

#if JACK_USE_MACH_THREADS 
		if (client->rt_thread_ok) {
			// MacOSX pthread_cancel not implemented in
			// Darwin 5.5, 6.4
			mach_port_t machThread =
				pthread_mach_thread_np (client->process_thread);
			thread_terminate (machThread);
		}
#endif
	
		/* stop the thread that communicates with the jack
		 * server, only if it was actually running 
		 */
		
		if (client->thread_ok){
			pthread_cancel (client->thread);
			pthread_join (client->thread, &status);
		}

		if (client->control) {
			jack_release_shm (&client->control_shm);
			client->control = NULL;
		}
		if (client->engine) {
			jack_release_shm (&client->engine_shm);
			client->engine = NULL;
		}

		if (client->port_segment) {
			jack_port_type_id_t ptid;
			for (ptid = 0; ptid < client->n_port_types; ++ptid) {
				jack_release_shm (&client->port_segment[ptid]);
			}
			free (client->port_segment);
			client->port_segment = NULL;
		}

#ifndef JACK_USE_MACH_THREADS
		if (client->graph_wait_fd) {
			close (client->graph_wait_fd);
		}
		
		if (client->graph_next_fd) {
			close (client->graph_next_fd);
		}
#endif		
		
		close (client->event_fd);

		if (shutdown (client->request_fd, SHUT_RDWR)) {
			jack_error ("could not shutdown client request socket");
		}

		close (client->request_fd);

	}

	for (node = client->ports; node; node = jack_slist_next (node)) {
		free (node->data);
	}
	jack_slist_free (client->ports);
	for (node = client->ports_ext; node; node = jack_slist_next (node)) {
		free (node->data);
	}
	jack_slist_free (client->ports_ext);
	jack_client_free (client);

	return rc;
}	

int 
jack_is_realtime (jack_client_t *client)
{
	return client->engine->real_time;
}

jack_nframes_t 
jack_get_buffer_size (jack_client_t *client)
{
	return client->engine->buffer_size;
}

int
jack_set_buffer_size (jack_client_t *client, jack_nframes_t nframes)
{
#ifdef DO_BUFFER_RESIZE
	jack_request_t req;

	req.type = SetBufferSize;
	req.x.nframes = nframes;

	return jack_client_deliver_request (client, &req);
#else
	return ENOSYS;

#endif /* DO_BUFFER_RESIZE */
}

int 
jack_connect (jack_client_t *client, const char *source_port,
	      const char *destination_port)
{
	jack_request_t req;

	req.type = ConnectPorts;

	snprintf (req.x.connect.source_port,
		  sizeof (req.x.connect.source_port), "%s", source_port);
	snprintf (req.x.connect.destination_port,
		  sizeof (req.x.connect.destination_port),
		  "%s", destination_port);

	return jack_client_deliver_request (client, &req);
}

int
jack_port_disconnect (jack_client_t *client, jack_port_t *port)
{
	jack_request_t req;

	pthread_mutex_lock (&port->connection_lock);

	if (port->connections == NULL) {
		pthread_mutex_unlock (&port->connection_lock);
		return 0;
	}

	pthread_mutex_unlock (&port->connection_lock);

	req.type = DisconnectPort;
	req.x.port_info.port_id = port->shared->id;

	return jack_client_deliver_request (client, &req);
}

int 
jack_disconnect (jack_client_t *client, const char *source_port,
		 const char *destination_port)
{
	jack_request_t req;

	req.type = DisconnectPorts;

	snprintf (req.x.connect.source_port,
		  sizeof (req.x.connect.source_port), "%s", source_port);
	snprintf (req.x.connect.destination_port,
		  sizeof (req.x.connect.destination_port),
		  "%s", destination_port);
	
	return jack_client_deliver_request (client, &req);
}

void
jack_set_error_function (void (*func) (const char *))
{
	jack_error_callback = func;
}

int 
jack_set_graph_order_callback (jack_client_t *client,
			       JackGraphOrderCallback callback, void *arg)
{
	if (client->control->active) {
		jack_error ("You cannot set callbacks on an active client.");
		return -1;
	}
	client->control->graph_order = callback;
	client->control->graph_order_arg = arg;
	return 0;
}

int jack_set_xrun_callback (jack_client_t *client,
			    JackXRunCallback callback, void *arg)
{
	if (client->control->active) {
		jack_error ("You cannot set callbacks on an active client.");
		return -1;
	}

	client->control->xrun = callback;
	client->control->xrun_arg = arg;
	return 0;       
}

int
jack_set_process_callback (jack_client_t *client,
			   JackProcessCallback callback, void *arg)

{
	if (client->control->active) {
		jack_error ("You cannot set callbacks on an active client.");
		return -1;
	}
	client->control->process_arg = arg;
	client->control->process = callback;
	return 0;
}

int
jack_set_thread_init_callback (jack_client_t *client,
			       JackThreadInitCallback callback, void *arg)

{
	if (client->control->active) {
		jack_error ("You cannot set callbacks on an active client.");
		return -1;
	}
	client->control->thread_init_arg = arg;
	client->control->thread_init = callback;
	return 0;
}

int
jack_set_freewheel_callback (jack_client_t *client,
			     JackFreewheelCallback callback, void *arg)
{
	if (client->control->active) {
		jack_error ("You cannot set callbacks on an active client.");
		return -1;
	}
	client->control->freewheel_arg = arg;
	client->control->freewheel_cb = callback;
	return 0;
}

int
jack_set_buffer_size_callback (jack_client_t *client,
			       JackBufferSizeCallback callback, void *arg)
{
	client->control->bufsize_arg = arg;
	client->control->bufsize = callback;
	return 0;
}

int
jack_set_port_registration_callback(jack_client_t *client,
				    JackPortRegistrationCallback callback,
				    void *arg)
{
	if (client->control->active) {
		jack_error ("You cannot set callbacks on an active client.");
		return -1;
	}
	client->control->port_register_arg = arg;
	client->control->port_register = callback;
	return 0;
}

int
jack_set_port_connect_callback(jack_client_t *client,
			       JackPortConnectCallback callback,
			       void *arg)
{
	if (client->control->active) {
		jack_error ("You cannot set callbacks on an active client.");
		return -1;
	}
	client->control->port_connect_arg = arg;
	client->control->port_connect = callback;
	return 0;
}

int
jack_set_client_registration_callback(jack_client_t *client,
				      JackClientRegistrationCallback callback,
				      void *arg)
{
	if (client->control->active) {
		jack_error ("You cannot set callbacks on an active client.");
		return -1;
	}
	client->control->client_register_arg = arg;
	client->control->client_register = callback;
	return 0;
}

int
jack_get_process_done_fd (jack_client_t *client)
{
	return client->graph_next_fd;
}

void
jack_on_shutdown (jack_client_t *client, void (*function)(void *arg), void *arg)
{
	client->on_shutdown = function;
	client->on_shutdown_arg = arg;
}

const char **
jack_get_ports (jack_client_t *client,
		const char *port_name_pattern,
		const char *type_name_pattern,
		unsigned long flags)
{
	jack_control_t *engine;
	const char **matching_ports;
	unsigned long match_cnt;
	jack_port_shared_t *psp;
	unsigned long i;
	regex_t port_regex;
	regex_t type_regex;
	int matching;

	engine = client->engine;

	if (port_name_pattern && port_name_pattern[0]) {
		regcomp (&port_regex, port_name_pattern,
			 REG_EXTENDED|REG_NOSUB);
	}
	if (type_name_pattern && type_name_pattern[0]) {
		regcomp (&type_regex, type_name_pattern,
			 REG_EXTENDED|REG_NOSUB);
	}

	psp = engine->ports;
	match_cnt = 0;

	matching_ports = (const char **)
		malloc (sizeof (char *) * engine->port_max);

	for (i = 0; i < engine->port_max; i++) {
		matching = 1;

		if (!psp[i].in_use) {
			continue;
		}

		if (flags) {
			if ((psp[i].flags & flags) != flags) {
				matching = 0;
			}
		}

		if (matching && port_name_pattern && port_name_pattern[0]) {
			if (regexec (&port_regex, psp[i].name, 0, NULL, 0)) {
				matching = 0;
			}
		} 

		if (matching && type_name_pattern && type_name_pattern[0]) {
			jack_port_type_id_t ptid = psp[i].ptype_id;
			if (regexec (&type_regex,
				     engine->port_types[ptid].type_name,
				     0, NULL, 0)) {
				matching = 0;
			}
		} 
		
		if (matching) {
			matching_ports[match_cnt++] = psp[i].name;
		}
	}
	if (port_name_pattern && port_name_pattern[0]) {
		regfree (&port_regex);
	}
	if (type_name_pattern && type_name_pattern[0]) {
		regfree (&type_regex);
	}

	matching_ports[match_cnt] = 0;

	if (match_cnt == 0) {
		free (matching_ports);
		matching_ports = 0;
	}

	return matching_ports;
}

float
jack_cpu_load (jack_client_t *client)
{
	return client->engine->cpu_load;
}

float
jack_get_xrun_delayed_usecs (jack_client_t *client)
{
	return client->engine->xrun_delayed_usecs;
}

float
jack_get_max_delayed_usecs (jack_client_t *client)
{
	return client->engine->max_delayed_usecs;
}

void
jack_reset_max_delayed_usecs (jack_client_t *client)
{
	client->engine->max_delayed_usecs =  0.0f;
}

pthread_t
jack_client_thread_id (jack_client_t *client)
{
	return client->thread_id;
}

int
jack_client_name_size(void)
{
	return JACK_CLIENT_NAME_SIZE;
}

int
jack_port_name_size(void)
{
	return JACK_PORT_NAME_SIZE;
}

int
jack_port_type_size(void)
{
	return JACK_PORT_TYPE_SIZE;
}

