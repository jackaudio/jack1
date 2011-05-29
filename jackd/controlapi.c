// u/* -*- Mode: C++ ; c-basic-offset: 4 -*- */
/*
  JACK control API implementation

  Copyright (C) 2008 Nedko Arnaudov
  Copyright (C) 2008 Grame

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef WIN32
#include <stdint.h>
#include <dirent.h>
#include <pthread.h>
#endif

#include "config.h"

#include "jack/internal.h"
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <signal.h>

#include "jack/jslist.h"
#include "jack/driver_interface.h"
#include "jack/driver.h"

#include "jack/engine.h"
//#include "JackError.h"
//#include "JackServer.h"
//#include "shm.h"
//#include "JackTools.h"
//#include "JackControlAPI.h"
//#include "JackLockedEngine.h"
//#include "JackConstants.h"
//#include "JackDriverLoader.h"
//#include "JackServerGlobals.h"

#include "jack/control.h"

/*
 * XXX: dont like statics here.
 */
static JSList *drivers = NULL;

struct jackctl_server
{
    JSList * drivers;
    JSList * internals;
    JSList * parameters;

    jack_engine_t * engine;

    /* string, server name */
    union jackctl_parameter_value name;
    union jackctl_parameter_value default_name;

    /* bool, whether to be "realtime" */
    union jackctl_parameter_value realtime;
    union jackctl_parameter_value default_realtime;

    /* int32_t */
    union jackctl_parameter_value realtime_priority;
    union jackctl_parameter_value default_realtime_priority;

    /* bool, whether to exit once all clients have closed their connections */
    union jackctl_parameter_value temporary;
    union jackctl_parameter_value default_temporary;

    /* bool, whether to be verbose */
    union jackctl_parameter_value verbose;
    union jackctl_parameter_value default_verbose;

    /* int32_t, msecs; if zero, use period size. */
    union jackctl_parameter_value client_timeout;
    union jackctl_parameter_value default_client_timeout;
    
    /* uint32_t, clock source type */
    union jackctl_parameter_value clock_source;
    union jackctl_parameter_value default_clock_source;
   
    /* uint32_t, max port number */
    union jackctl_parameter_value port_max;
    union jackctl_parameter_value default_port_max;
    
    /* bool */
    union jackctl_parameter_value replace_registry;
    union jackctl_parameter_value default_replace_registry;

    /* bool, use mlock */
    union jackctl_parameter_value do_mlock;
    union jackctl_parameter_value default_do_mlock;

    /* bool, munlock gui libraries */
    union jackctl_parameter_value do_unlock;
    union jackctl_parameter_value default_do_unlock;

    /* bool, dont zombify... */
    union jackctl_parameter_value nozombies;
    union jackctl_parameter_value default_nozombies;
};

struct jackctl_driver
{
    jack_driver_desc_t * desc_ptr;
    JSList * parameters;
    JSList * set_parameters;
};

struct jackctl_internal
{
    jack_driver_desc_t * desc_ptr;
    JSList * parameters;
    JSList * set_parameters;
    int refnum;
};

struct jackctl_parameter
{
    const char * name;
    const char * short_description;
    const char * long_description;
    jackctl_param_type_t type;
    bool is_set;
    union jackctl_parameter_value * value_ptr;
    union jackctl_parameter_value * default_value_ptr;

    union jackctl_parameter_value value;
    union jackctl_parameter_value default_value;
    struct jackctl_driver * driver_ptr;
    char id;
    jack_driver_param_t * driver_parameter_ptr;
    jack_driver_param_constraint_desc_t * constraint_ptr;
};

static
struct jackctl_parameter *
jackctl_add_parameter(
    JSList ** parameters_list_ptr_ptr,
    const char * name,
    const char * short_description,
    const char * long_description,
    jackctl_param_type_t type,
    union jackctl_parameter_value * value_ptr,
    union jackctl_parameter_value * default_value_ptr,
    union jackctl_parameter_value value,
    jack_driver_param_constraint_desc_t * constraint_ptr)
{
    struct jackctl_parameter * parameter_ptr;

    parameter_ptr = (struct jackctl_parameter *)malloc(sizeof(struct jackctl_parameter));
    if (parameter_ptr == NULL)
    {
        jack_error("Cannot allocate memory for jackctl_parameter structure.");
        goto fail;
    }

    parameter_ptr->name = name;
    parameter_ptr->short_description = short_description;
    parameter_ptr->long_description = long_description;
    parameter_ptr->type = type;
    parameter_ptr->is_set = false;

    if (value_ptr == NULL)
    {
        value_ptr = &parameter_ptr->value;
    }

    if (default_value_ptr == NULL)
    {
        default_value_ptr = &parameter_ptr->default_value;
    }

    parameter_ptr->value_ptr = value_ptr;
    parameter_ptr->default_value_ptr = default_value_ptr;

    *value_ptr = *default_value_ptr = value;

    parameter_ptr->driver_ptr = NULL;
    parameter_ptr->driver_parameter_ptr = NULL;
    parameter_ptr->id = 0;
    parameter_ptr->constraint_ptr = constraint_ptr;

    *parameters_list_ptr_ptr = jack_slist_append(*parameters_list_ptr_ptr, parameter_ptr);

    return parameter_ptr;

fail:
    return NULL;
}

static
void
jackctl_free_driver_parameters(
    struct jackctl_driver * driver_ptr)
{
    JSList * next_node_ptr;

    while (driver_ptr->parameters)
    {
        next_node_ptr = driver_ptr->parameters->next;
        free(driver_ptr->parameters->data);
        free(driver_ptr->parameters);
        driver_ptr->parameters = next_node_ptr;
    }

    while (driver_ptr->set_parameters)
    {
        next_node_ptr = driver_ptr->set_parameters->next;
        free(driver_ptr->set_parameters->data);
        free(driver_ptr->set_parameters);
        driver_ptr->set_parameters = next_node_ptr;
    }
}

static
bool
jackctl_add_driver_parameters(
    struct jackctl_driver * driver_ptr)
{
    uint32_t i;
    union jackctl_parameter_value jackctl_value;
    jackctl_param_type_t jackctl_type;
    struct jackctl_parameter * parameter_ptr;
    jack_driver_param_desc_t * descriptor_ptr;

    for (i = 0 ; i < driver_ptr->desc_ptr->nparams ; i++)
    {
        descriptor_ptr = driver_ptr->desc_ptr->params + i;

        switch (descriptor_ptr->type)
        {
        case JackDriverParamInt:
            jackctl_type = JackParamInt;
            jackctl_value.i = descriptor_ptr->value.i;
            break;
        case JackDriverParamUInt:
            jackctl_type = JackParamUInt;
            jackctl_value.ui = descriptor_ptr->value.ui;
            break;
        case JackDriverParamChar:
            jackctl_type = JackParamChar;
            jackctl_value.c = descriptor_ptr->value.c;
            break;
        case JackDriverParamString:
            jackctl_type = JackParamString;
            strcpy(jackctl_value.str, descriptor_ptr->value.str);
            break;
        case JackDriverParamBool:
            jackctl_type = JackParamBool;
            jackctl_value.b = descriptor_ptr->value.i;
            break;
        default:
            jack_error("unknown driver parameter type %i", (int)descriptor_ptr->type);
            assert(0);
            goto fail;
        }

        parameter_ptr = jackctl_add_parameter(
            &driver_ptr->parameters,
            descriptor_ptr->name,
            descriptor_ptr->short_desc,
            descriptor_ptr->long_desc,
            jackctl_type,
            NULL,
            NULL,
            jackctl_value,
            descriptor_ptr->constraint);

        if (parameter_ptr == NULL)
        {
            goto fail;
        }

        parameter_ptr->driver_ptr = driver_ptr;
        parameter_ptr->id = descriptor_ptr->character;
    }

    return true;

fail:
    jackctl_free_driver_parameters(driver_ptr);

    return false;
}

static jack_driver_desc_t *
jack_drivers_get_descriptor (JSList * drivers, const char * sofile)
{
	jack_driver_desc_t * descriptor, * other_descriptor;
	JackDriverDescFunction so_get_descriptor;
	JSList * node;
	void * dlhandle;
	char * filename;
	const char * dlerr;
	int err;
	char* driver_dir;

	if ((driver_dir = getenv("JACK_DRIVER_DIR")) == 0) {
		driver_dir = ADDON_DIR;
	}
	filename = malloc (strlen (driver_dir) + 1 + strlen (sofile) + 1);
	sprintf (filename, "%s/%s", driver_dir, sofile);

//	if (verbose) {
//		jack_info ("getting driver descriptor from %s", filename);
//	}

	if ((dlhandle = dlopen (filename, RTLD_NOW|RTLD_GLOBAL)) == NULL) {
		jack_error ("could not open driver .so '%s': %s\n", filename, dlerror ());
		free (filename);
		return NULL;
	}

	so_get_descriptor = (JackDriverDescFunction)
		dlsym (dlhandle, "driver_get_descriptor");

	if ((dlerr = dlerror ()) != NULL) {
		jack_error("%s", dlerr);
		dlclose (dlhandle);
		free (filename);
		return NULL;
	}

	if ((descriptor = so_get_descriptor ()) == NULL) {
		jack_error ("driver from '%s' returned NULL descriptor\n", filename);
		dlclose (dlhandle);
		free (filename);
		return NULL;
	}

	if ((err = dlclose (dlhandle)) != 0) {
		jack_error ("error closing driver .so '%s': %s\n", filename, dlerror ());
	}

	/* check it doesn't exist already */
	for (node = drivers; node; node = jack_slist_next (node)) {
		other_descriptor = (jack_driver_desc_t *) node->data;

		if (strcmp (descriptor->name, other_descriptor->name) == 0) {
			jack_error ("the drivers in '%s' and '%s' both have the name '%s'; using the first\n",
				    other_descriptor->file, filename, other_descriptor->name);
			/* FIXME: delete the descriptor */
			free (filename);
			return NULL;
		}
	}

	snprintf (descriptor->file, sizeof(descriptor->file), "%s", filename);
	free (filename);

	return descriptor;
}

static JSList *
jack_drivers_load ()
{
	struct dirent * dir_entry;
	DIR * dir_stream;
	const char * ptr;
	int err;
	JSList * driver_list = NULL;
	jack_driver_desc_t * desc;
	char* driver_dir;

	if ((driver_dir = getenv("JACK_DRIVER_DIR")) == 0) {
		driver_dir = ADDON_DIR;
	}

	/* search through the driver_dir and add get descriptors
	   from the .so files in it */
	dir_stream = opendir (driver_dir);
	if (!dir_stream) {
		jack_error ("could not open driver directory %s: %s\n",
			    driver_dir, strerror (errno));
		return NULL;
	}
  
	while ( (dir_entry = readdir (dir_stream)) ) {
		/* check the filename is of the right format */
		if (strncmp ("jack_", dir_entry->d_name, 5) != 0) {
			continue;
		}

		ptr = strrchr (dir_entry->d_name, '.');
		if (!ptr) {
			continue;
		}
		ptr++;
		if (strncmp ("so", ptr, 2) != 0) {
			continue;
		}

		desc = jack_drivers_get_descriptor (drivers, dir_entry->d_name);
		if (desc) {
			driver_list = jack_slist_append (driver_list, desc);
		}
	}

	err = closedir (dir_stream);
	if (err) {
		jack_error ("error closing driver directory %s: %s\n",
			    driver_dir, strerror (errno));
	}

	if (!driver_list) {
		jack_error ("could not find any drivers in %s!\n", driver_dir);
		return NULL;
	}

	return driver_list;
}

static void
jack_cleanup_files (const char *server_name)
{
	DIR *dir;
	struct dirent *dirent;
	char dir_name[PATH_MAX+1] = "";
        jack_server_dir (server_name, dir_name);

	/* On termination, we remove all files that jackd creates so
	 * subsequent attempts to start jackd will not believe that an
	 * instance is already running.  If the server crashes or is
	 * terminated with SIGKILL, this is not possible.  So, cleanup
	 * is also attempted when jackd starts.
	 *
	 * There are several tricky issues.  First, the previous JACK
	 * server may have run for a different user ID, so its files
	 * may be inaccessible.  This is handled by using a separate
	 * JACK_TMP_DIR subdirectory for each user.  Second, there may
	 * be other servers running with different names.  Each gets
	 * its own subdirectory within the per-user directory.  The
	 * current process has already registered as `server_name', so
	 * we know there is no other server actively using that name.
	 */

	/* nothing to do if the server directory does not exist */
	if ((dir = opendir (dir_name)) == NULL) {
		return;
	}

	/* unlink all the files in this directory, they are mine */
	while ((dirent = readdir (dir)) != NULL) {

		char fullpath[PATH_MAX+1];

		if ((strcmp (dirent->d_name, ".") == 0)
		    || (strcmp (dirent->d_name, "..") == 0)) {
			continue;
		}

		snprintf (fullpath, sizeof (fullpath), "%s/%s",
			  dir_name, dirent->d_name);

		if (unlink (fullpath)) {
			jack_error ("cannot unlink `%s' (%s)", fullpath,
				    strerror (errno));
		}
	} 

	closedir (dir);

	/* now, delete the per-server subdirectory, itself */
	if (rmdir (dir_name)) {
 		jack_error ("cannot remove `%s' (%s)", dir_name,
			    strerror (errno));
	}

	/* finally, delete the per-user subdirectory, if empty */
	if (rmdir (jack_user_dir ())) {
		if (errno != ENOTEMPTY) {
			jack_error ("cannot remove `%s' (%s)",
				    jack_user_dir (), strerror (errno));
		}
	}
}

static int
jackctl_drivers_load(
    struct jackctl_server * server_ptr)
{
    struct jackctl_driver * driver_ptr;
    JSList *node_ptr;
    JSList *descriptor_node_ptr;

    descriptor_node_ptr = jack_drivers_load();
    if (descriptor_node_ptr == NULL)
    {
        jack_error("could not find any drivers in driver directory!");
        return false;
    }

    while (descriptor_node_ptr != NULL)
    {
        driver_ptr = (struct jackctl_driver *)malloc(sizeof(struct jackctl_driver));
        if (driver_ptr == NULL)
        {
            jack_error("memory allocation of jackctl_driver structure failed.");
            goto next;
        }

        driver_ptr->desc_ptr = (jack_driver_desc_t *)descriptor_node_ptr->data;
        driver_ptr->parameters = NULL;
        driver_ptr->set_parameters = NULL;

        if (!jackctl_add_driver_parameters(driver_ptr))
        {
            assert(driver_ptr->parameters == NULL);
            free(driver_ptr);
            goto next;
        }

        server_ptr->drivers = jack_slist_append(server_ptr->drivers, driver_ptr);

    next:
        node_ptr = descriptor_node_ptr;
        descriptor_node_ptr = descriptor_node_ptr->next;
        free(node_ptr);
    }

    return true;
}

static
void
jackctl_server_free_drivers(
    struct jackctl_server * server_ptr)
{
    JSList * next_node_ptr;
    struct jackctl_driver * driver_ptr;

    while (server_ptr->drivers)
    {
        next_node_ptr = server_ptr->drivers->next;
        driver_ptr = (struct jackctl_driver *)server_ptr->drivers->data;

        jackctl_free_driver_parameters(driver_ptr);
        free(driver_ptr->desc_ptr->params);
        free(driver_ptr->desc_ptr);
        free(driver_ptr);

        free(server_ptr->drivers);
        server_ptr->drivers = next_node_ptr;
    }
}

static int
jackctl_internals_load(
    struct jackctl_server * server_ptr)
{
    struct jackctl_internal * internal_ptr;
    JSList *node_ptr;
    JSList *descriptor_node_ptr = NULL;

    //descriptor_node_ptr = jack_internals_load(NULL);
    if (descriptor_node_ptr == NULL)
    {
        return false;
    }

    while (descriptor_node_ptr != NULL)
    {     
        internal_ptr = (struct jackctl_internal *)malloc(sizeof(struct jackctl_internal));
        if (internal_ptr == NULL)
        {
            jack_error("memory allocation of jackctl_driver structure failed.");
            goto next;
        }

        internal_ptr->desc_ptr = (jack_driver_desc_t *)descriptor_node_ptr->data;
        internal_ptr->parameters = NULL;
        internal_ptr->set_parameters = NULL;

        if (!jackctl_add_driver_parameters((struct jackctl_driver *)internal_ptr))
        {
            assert(internal_ptr->parameters == NULL);
            free(internal_ptr);
            goto next;
        }

        server_ptr->internals = jack_slist_append(server_ptr->internals, internal_ptr);

    next:
        node_ptr = descriptor_node_ptr;
        descriptor_node_ptr = descriptor_node_ptr->next;
        free(node_ptr);
    }

    return true;
}

static
void
jackctl_server_free_internals(
    struct jackctl_server * server_ptr)
{
    JSList * next_node_ptr;
    struct jackctl_internal * internal_ptr;

    while (server_ptr->internals)
    {
        next_node_ptr = server_ptr->internals->next;
        internal_ptr = (struct jackctl_internal *)server_ptr->internals->data;

        jackctl_free_driver_parameters((struct jackctl_driver *)internal_ptr);
        free(internal_ptr->desc_ptr->params);
        free(internal_ptr->desc_ptr);
        free(internal_ptr);

        free(server_ptr->internals);
        server_ptr->internals = next_node_ptr;
    }
}

static
void
jackctl_server_free_parameters(
    struct jackctl_server * server_ptr)
{
    JSList * next_node_ptr;

    while (server_ptr->parameters)
    {
        next_node_ptr = server_ptr->parameters->next;
        free(server_ptr->parameters->data);
        free(server_ptr->parameters);
        server_ptr->parameters = next_node_ptr;
    }
}

#ifdef WIN32

static HANDLE waitEvent;

static void do_nothing_handler(int signum)
{
    printf("jack main caught signal %d\n", signum);
    (void) signal(SIGINT, SIG_DFL);
    SetEvent(waitEvent);
}

sigset_t
jackctl_setup_signals(
    unsigned int flags)
{
        if ((waitEvent = CreateEvent(NULL, FALSE, FALSE, NULL)) == NULL) {
        jack_error("CreateEvent fails err = %ld", GetLastError());
        return 0;
    }

        (void) signal(SIGINT, do_nothing_handler);
    (void) signal(SIGABRT, do_nothing_handler);
    (void) signal(SIGTERM, do_nothing_handler);

        return (sigset_t)waitEvent;
}

void jackctl_wait_signals(sigset_t signals)
{
        if (WaitForSingleObject(waitEvent, INFINITE) != WAIT_OBJECT_0) {
        jack_error("WaitForSingleObject fails err = %ld", GetLastError());
    }
}

#else

static
void
do_nothing_handler(int sig)
{
    /* this is used by the child (active) process, but it never
       gets called unless we are already shutting down after
       another signal.
    */
    char buf[64];
    snprintf (buf, sizeof(buf), "received signal %d during shutdown (ignored)\n", sig);
}

sigset_t
jackctl_setup_signals(
    unsigned int flags)
{
    sigset_t signals;
    sigset_t allsignals;
    struct sigaction action;
    int i;

    /* ensure that we are in our own process group so that
       kill (SIG, -pgrp) does the right thing.
    */

    setsid();

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    /* what's this for?

       POSIX says that signals are delivered like this:

       * if a thread has blocked that signal, it is not
           a candidate to receive the signal.
           * of all threads not blocking the signal, pick
           one at random, and deliver the signal.

           this means that a simple-minded multi-threaded program can
           expect to get POSIX signals delivered randomly to any one
           of its threads,

       here, we block all signals that we think we might receive
       and want to catch. all "child" threads will inherit this
       setting. if we create a thread that calls sigwait() on the
       same set of signals, implicitly unblocking all those
       signals. any of those signals that are delivered to the
       process will be delivered to that thread, and that thread
       alone. this makes cleanup for a signal-driven exit much
       easier, since we know which thread is doing it and more
       importantly, we are free to call async-unsafe functions,
       because the code is executing in normal thread context
       after a return from sigwait().
    */

    sigemptyset(&signals);
    sigaddset(&signals, SIGHUP);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGQUIT);
    sigaddset(&signals, SIGPIPE);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGUSR1);
    sigaddset(&signals, SIGUSR2);

    /* all child threads will inherit this mask unless they
     * explicitly reset it
     */

     pthread_sigmask(SIG_BLOCK, &signals, 0);

    /* install a do-nothing handler because otherwise pthreads
       behaviour is undefined when we enter sigwait.
    */

    sigfillset(&allsignals);
    action.sa_handler = do_nothing_handler;
    action.sa_mask = allsignals;
    action.sa_flags = SA_RESTART|SA_RESETHAND;

    for (i = 1; i < NSIG; i++)
    {
        if (sigismember (&signals, i))
        {
            sigaction(i, &action, 0);
        }
    }

    return signals;
}

void
jackctl_wait_signals(sigset_t signals)
{
    int sig;
    bool waiting = true;

    while (waiting) {
    #if defined(sun) && !defined(__sun__) // SUN compiler only, to check
        sigwait(&signals);
    #else
        sigwait(&signals, &sig);
    #endif
        fprintf(stderr, "jack main caught signal %d\n", sig);

        switch (sig) {
            case SIGUSR1:
                //jack_dump_configuration(engine, 1);
                break;
            case SIGUSR2:
                // driver exit
                waiting = false;
                break;
            case SIGTTOU:
                break;
            default:
                waiting = false;
                break;
        }
    }

    if (sig != SIGSEGV) {
        // unblock signals so we can see them during shutdown.
        // this will help prod developers not to lose sight of
        // bugs that cause segfaults etc. during shutdown.
        sigprocmask(SIG_UNBLOCK, &signals, 0);
    }
}
#endif

static sigset_t
jackctl_block_signals()
{
    sigset_t signals;
    sigset_t oldsignals;

    sigemptyset(&signals);
    sigaddset(&signals, SIGHUP);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGQUIT);
    sigaddset(&signals, SIGPIPE);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGUSR1);
    sigaddset(&signals, SIGUSR2);

    pthread_sigmask(SIG_BLOCK, &signals, &oldsignals);

    return oldsignals;
}

static void
jackctl_unblock_signals(sigset_t oldsignals)
{
    pthread_sigmask(SIG_SETMASK, &oldsignals, 0);
}


static
jack_driver_param_constraint_desc_t *
get_realtime_priority_constraint()
{
#ifndef __OpenBSD__
    jack_driver_param_constraint_desc_t * constraint_ptr;

    int max = sched_get_priority_max (SCHED_FIFO);
    int min = sched_get_priority_min (SCHED_FIFO);

    //jack_info("realtime priority range is (%d,%d)", min, max);

    constraint_ptr = (jack_driver_param_constraint_desc_t *)calloc(1, sizeof(jack_driver_param_value_enum_t));
    if (constraint_ptr == NULL)
    {
        jack_error("Cannot allocate memory for jack_driver_param_constraint_desc_t structure.");
        return NULL;
    }
    constraint_ptr->flags = JACK_CONSTRAINT_FLAG_RANGE;

    constraint_ptr->constraint.range.min.i = min;
    constraint_ptr->constraint.range.max.i = max;

    return constraint_ptr;
#else
    return NULL
#endif
}

jackctl_server_t * jackctl_server_create(
    bool (* on_device_acquire)(const char * device_name),
    void (* on_device_release)(const char * device_name))
{
    struct jackctl_server * server_ptr;
    union jackctl_parameter_value value;

    server_ptr = (struct jackctl_server *)malloc(sizeof(struct jackctl_server));
    if (server_ptr == NULL)
    {
        jack_error("Cannot allocate memory for jackctl_server structure.");
        goto fail;
    }

    server_ptr->drivers = NULL;
    server_ptr->internals = NULL;
    server_ptr->parameters = NULL;
    server_ptr->engine = NULL;

    strcpy(value.str, jack_default_server_name() );
    if (jackctl_add_parameter(
            &server_ptr->parameters,
            "name",
            "Server name to use.",
            "",
            JackParamString,
            &server_ptr->name,
            &server_ptr->default_name,
            value, NULL) == NULL)
    {
        goto fail_free_parameters;
    }

    value.b = false;
    if (jackctl_add_parameter(
            &server_ptr->parameters,
            "realtime",
            "Whether to use realtime mode.",
            "Use realtime scheduling. This is needed for reliable low-latency performance. On most systems, it requires JACK to run with special scheduler and memory allocation privileges, which may be obtained in several ways. On Linux you should use PAM.",
            JackParamBool,
            &server_ptr->realtime,
            &server_ptr->default_realtime,
            value, NULL) == NULL)
    {
        goto fail_free_parameters;
    }

    value.i = 10;
    if (jackctl_add_parameter(
            &server_ptr->parameters,
            "realtime-priority",
            "Scheduler priority when running in realtime mode.",
            "",
            JackParamInt,
            &server_ptr->realtime_priority,
            &server_ptr->default_realtime_priority,
            value,
            get_realtime_priority_constraint()
	    ) == NULL)
    {
        goto fail_free_parameters;
    }

    value.b = false;
    if (jackctl_add_parameter(
            &server_ptr->parameters,
            "temporary",
            "Exit once all clients have closed their connections.",
            "",
            JackParamBool,
            &server_ptr->temporary,
            &server_ptr->default_temporary,
            value, NULL) == NULL)
    {
        goto fail_free_parameters;
    }

    value.b = false;
    if (jackctl_add_parameter(
            &server_ptr->parameters,
            "verbose",
            "Verbose mode.",
            "",
            JackParamBool,
            &server_ptr->verbose,
            &server_ptr->default_verbose,
            value, NULL) == NULL)
    {
        goto fail_free_parameters;
    }

    value.i = 0;
    if (jackctl_add_parameter(
            &server_ptr->parameters,
            "client-timeout",
            "Client timeout limit in milliseconds.",
            "",
            JackParamInt,
            &server_ptr->client_timeout,
            &server_ptr->default_client_timeout,
            value, NULL) == NULL)
    {
        goto fail_free_parameters;
    }

    value.ui = 0;
    if (jackctl_add_parameter(
            &server_ptr->parameters,
            "clock-source",
            "Clocksource type : c(ycle) | h(pet) | s(ystem).",
            "",
            JackParamUInt,
            &server_ptr->clock_source,
            &server_ptr->default_clock_source,
            value, NULL) == NULL)
    {
        goto fail_free_parameters;
    }
    
    value.ui = 128;
    if (jackctl_add_parameter(
          &server_ptr->parameters,
          "port-max",
          "Maximum number of ports.",
          "",
          JackParamUInt,
          &server_ptr->port_max,
          &server_ptr->default_port_max,
          value, NULL) == NULL)
    {
        goto fail_free_parameters;
    }

    value.b = false;
    if (jackctl_add_parameter(
            &server_ptr->parameters,
            "replace-registry",
            "Replace shared memory registry.",
            "",
            JackParamBool,
            &server_ptr->replace_registry,
            &server_ptr->default_replace_registry,
            value, NULL) == NULL)
    {
        goto fail_free_parameters;
    }

    value.b = false;
    if (jackctl_add_parameter(
            &server_ptr->parameters,
            "mlock",
            "Use mlock.",
            "",
            JackParamBool,
            &server_ptr->do_mlock,
            &server_ptr->default_do_mlock,
            value, NULL) == NULL)
    {
        goto fail_free_parameters;
    }

    value.b = false;
    if (jackctl_add_parameter(
            &server_ptr->parameters,
            "unlock",
            "munlock memory for big libraries",
            "",
            JackParamBool,
            &server_ptr->do_unlock,
            &server_ptr->default_do_unlock,
            value, NULL) == NULL)
    {
        goto fail_free_parameters;
    }

    value.b = false;
    if (jackctl_add_parameter(
            &server_ptr->parameters,
            "nozombies",
            "dont zombifiy offending clients",
            "",
            JackParamBool,
            &server_ptr->nozombies,
            &server_ptr->default_nozombies,
            value, NULL) == NULL)
    {
        goto fail_free_parameters;
    }

    //TODO: need 
    //JackServerGlobals::on_device_acquire = on_device_acquire;
    //JackServerGlobals::on_device_release = on_device_release;

    if (!jackctl_drivers_load(server_ptr))
    {
        goto fail_free_parameters;
    }
    
    /* Allowed to fail */
    jackctl_internals_load(server_ptr);

    return server_ptr;

fail_free_parameters:
    jackctl_server_free_parameters(server_ptr);

    free(server_ptr);

fail:
    return NULL;
}

void jackctl_server_destroy(jackctl_server_t *server_ptr)
{
    jackctl_server_free_drivers(server_ptr);
    jackctl_server_free_internals(server_ptr);
    jackctl_server_free_parameters(server_ptr);
    free(server_ptr);
}

const JSList * jackctl_server_get_drivers_list(jackctl_server_t *server_ptr)
{
    return server_ptr->drivers;
}

bool jackctl_server_stop(jackctl_server_t *server_ptr)
{
	//jack_engine_driver_exit (server_ptr->engine);
	jack_engine_delete (server_ptr->engine);

    /* clean up shared memory and files from this server instance */
    //jack_log("cleaning up shared memory");

    jack_cleanup_shm();

    //jack_log("cleaning up files");

    jack_cleanup_files (server_ptr->name.str);

    //jack_log("unregistering server `%s'", server_ptr->name.str);

    jack_unregister_server(server_ptr->name.str);

    server_ptr->engine = NULL;

    return true;
}

const JSList * jackctl_server_get_parameters(jackctl_server_t *server_ptr)
{
    return server_ptr->parameters;
}

bool
jackctl_server_start(
    jackctl_server_t *server_ptr,
    jackctl_driver_t *driver_ptr)
{
    int rc;
    sigset_t oldsignals;


    // TODO:
    int frame_time_offset = 0;

    rc = jack_register_server (server_ptr->name.str, server_ptr->replace_registry.b);
    switch (rc)
    {
    case EEXIST:
        jack_error("`%s' server already active", server_ptr->name.str);
        goto fail;
    case ENOSPC:
        jack_error("too many servers already active");
        goto fail;
    case ENOMEM:
        jack_error("no access to shm registry");
        goto fail;
    }

    //jack_log("server `%s' registered", server_ptr->name.str);

    /* clean up shared memory and files from any previous
     * instance of this server name */
    jack_cleanup_shm ();
    jack_cleanup_files (server_ptr->name.str);

    if (!server_ptr->realtime.b && server_ptr->client_timeout.i == 0)
        server_ptr->client_timeout.i = 500; /* 0.5 sec; usable when non realtime. */
    
    oldsignals = jackctl_block_signals();

    if ((server_ptr->engine = jack_engine_new (server_ptr->realtime.b, server_ptr->realtime_priority.i, 
				    server_ptr->do_mlock.b, server_ptr->do_unlock.b, server_ptr->name.str,
				    server_ptr->temporary.b, server_ptr->verbose.b, server_ptr->client_timeout.i,
				    server_ptr->port_max.i, getpid(), frame_time_offset, 
				    server_ptr->nozombies.b, drivers)) == 0) {
	    jack_error ("cannot create engine");
	    goto fail_unregister;
    }

    if (jack_engine_load_driver (server_ptr->engine, driver_ptr->desc_ptr, driver_ptr->set_parameters))
    {
		jack_error ("cannot load driver module %s", driver_ptr->desc_ptr->name);
		goto fail_delete;
    }

    if (server_ptr->engine->driver->start (server_ptr->engine->driver) != 0) {
	    jack_error ("cannot start driver");
	    goto fail_close;
    }

    jackctl_unblock_signals( oldsignals );
    return true;

fail_close:

fail_delete:
    jack_engine_delete (server_ptr->engine);
    server_ptr->engine = NULL;

fail_unregister:
    //jack_log("cleaning up shared memory");

    jack_cleanup_shm();

    //jack_log("cleaning up files");

    jack_cleanup_files(server_ptr->name.str);

    //jack_log("unregistering server `%s'", server_ptr->name.str);

    jack_unregister_server(server_ptr->name.str);
    jackctl_unblock_signals( oldsignals );

fail:
    return false;
}

const char * jackctl_driver_get_name(jackctl_driver_t *driver_ptr)
{
    return driver_ptr->desc_ptr->name;
}

const JSList * jackctl_driver_get_parameters(jackctl_driver_t *driver_ptr)
{
    return driver_ptr->parameters;
}

jack_driver_desc_t * jackctl_driver_get_desc(jackctl_driver_t *driver_ptr)
{
    return driver_ptr->desc_ptr;
}

const char * jackctl_parameter_get_name(jackctl_parameter_t *parameter_ptr)
{
    return parameter_ptr->name;
}

const char * jackctl_parameter_get_short_description(jackctl_parameter_t *parameter_ptr)
{
    return parameter_ptr->short_description;
}

const char * jackctl_parameter_get_long_description(jackctl_parameter_t *parameter_ptr)
{
    return parameter_ptr->long_description;
}

bool jackctl_parameter_has_range_constraint(jackctl_parameter_t *parameter_ptr)
{
    return parameter_ptr->constraint_ptr != NULL && (parameter_ptr->constraint_ptr->flags & JACK_CONSTRAINT_FLAG_RANGE) != 0;
}

bool jackctl_parameter_has_enum_constraint(jackctl_parameter_t *parameter_ptr)
{
    return parameter_ptr->constraint_ptr != NULL && (parameter_ptr->constraint_ptr->flags & JACK_CONSTRAINT_FLAG_RANGE) == 0;
}

uint32_t jackctl_parameter_get_enum_constraints_count(jackctl_parameter_t *parameter_ptr)
{
    if (!jackctl_parameter_has_enum_constraint(parameter_ptr))
    {
        return 0;
    }

    return parameter_ptr->constraint_ptr->constraint.enumeration.count;
}

union jackctl_parameter_value jackctl_parameter_get_enum_constraint_value(jackctl_parameter_t *parameter_ptr, uint32_t index)
{
    jack_driver_param_value_t * value_ptr;
    union jackctl_parameter_value jackctl_value;

    value_ptr = &parameter_ptr->constraint_ptr->constraint.enumeration.possible_values_array[index].value;

    switch (parameter_ptr->type)
    {
    case JackParamInt:
        jackctl_value.i = value_ptr->i;
        break;
    case JackParamUInt:
        jackctl_value.ui = value_ptr->ui;
        break;
    case JackParamChar:
        jackctl_value.c = value_ptr->c;
        break;
    case JackParamString:
        strcpy(jackctl_value.str, value_ptr->str);
        break;
    default:
        jack_error("bad driver parameter type %i (enum constraint)", (int)parameter_ptr->type);
        assert(0);
    }

    return jackctl_value;
}

const char * jackctl_parameter_get_enum_constraint_description(jackctl_parameter_t *parameter_ptr, uint32_t index)
{
    return parameter_ptr->constraint_ptr->constraint.enumeration.possible_values_array[index].short_desc;
}

void jackctl_parameter_get_range_constraint(jackctl_parameter_t *parameter_ptr, union jackctl_parameter_value * min_ptr, union jackctl_parameter_value * max_ptr)
{
    switch (parameter_ptr->type)
    {
    case JackParamInt:
        min_ptr->i = parameter_ptr->constraint_ptr->constraint.range.min.i;
        max_ptr->i = parameter_ptr->constraint_ptr->constraint.range.max.i;
        return;
    case JackParamUInt:
        min_ptr->ui = parameter_ptr->constraint_ptr->constraint.range.min.ui;
        max_ptr->ui = parameter_ptr->constraint_ptr->constraint.range.max.ui;
        return;
    default:
        jack_error("bad driver parameter type %i (range constraint)", (int)parameter_ptr->type);
        assert(0);
    }
}

bool jackctl_parameter_constraint_is_strict(jackctl_parameter_t * parameter_ptr)
{
    return parameter_ptr->constraint_ptr != NULL && (parameter_ptr->constraint_ptr->flags & JACK_CONSTRAINT_FLAG_STRICT) != 0;
}

bool jackctl_parameter_constraint_is_fake_value(jackctl_parameter_t * parameter_ptr)
{
    return parameter_ptr->constraint_ptr != NULL && (parameter_ptr->constraint_ptr->flags & JACK_CONSTRAINT_FLAG_FAKE_VALUE) != 0;
}

jackctl_param_type_t jackctl_parameter_get_type(jackctl_parameter_t *parameter_ptr)
{
    return parameter_ptr->type;
}

char jackctl_parameter_get_id(jackctl_parameter_t * parameter_ptr)
{
    return parameter_ptr->id;
}

bool jackctl_parameter_is_set(jackctl_parameter_t *parameter_ptr)
{
    return parameter_ptr->is_set;
}

union jackctl_parameter_value jackctl_parameter_get_value(jackctl_parameter_t *parameter_ptr)
{
    return *(parameter_ptr->value_ptr);
}

bool jackctl_parameter_reset(jackctl_parameter_t *parameter_ptr)
{
    if (!parameter_ptr->is_set)
    {
        return true;
    }

    parameter_ptr->is_set = false;

    *parameter_ptr->value_ptr = *parameter_ptr->default_value_ptr;

    return true;
}

bool jackctl_parameter_set_value(jackctl_parameter_t *parameter_ptr, const union jackctl_parameter_value * value_ptr)
{
    bool new_driver_parameter;

    /* for driver parameters, set the parameter by adding jack_driver_param_t in the set_parameters list */
    if (parameter_ptr->driver_ptr != NULL)
    {
/*      jack_info("setting driver parameter %p ...", parameter_ptr); */
        new_driver_parameter = parameter_ptr->driver_parameter_ptr == NULL;
        if (new_driver_parameter)
        {
/*          jack_info("new driver parameter..."); */
            parameter_ptr->driver_parameter_ptr = (jack_driver_param_t *)malloc(sizeof(jack_driver_param_t));
            if (parameter_ptr->driver_parameter_ptr == NULL)
            {
                jack_error ("Allocation of jack_driver_param_t structure failed");
                return false;
            }

           parameter_ptr->driver_parameter_ptr->character = parameter_ptr->id;
           parameter_ptr->driver_ptr->set_parameters = jack_slist_append(parameter_ptr->driver_ptr->set_parameters, parameter_ptr->driver_parameter_ptr);
        }

        switch (parameter_ptr->type)
        {
        case JackParamInt:
            parameter_ptr->driver_parameter_ptr->value.i = value_ptr->i;
            break;
        case JackParamUInt:
            parameter_ptr->driver_parameter_ptr->value.ui = value_ptr->ui;
            break;
        case JackParamChar:
            parameter_ptr->driver_parameter_ptr->value.c = value_ptr->c;
            break;
        case JackParamString:
            strcpy(parameter_ptr->driver_parameter_ptr->value.str, value_ptr->str);
            break;
        case JackParamBool:
            parameter_ptr->driver_parameter_ptr->value.i = value_ptr->b;
            break;
        default:
            jack_error("unknown parameter type %i", (int)parameter_ptr->type);
            assert(0);

            if (new_driver_parameter)
            {
                parameter_ptr->driver_ptr->set_parameters = jack_slist_remove(parameter_ptr->driver_ptr->set_parameters, parameter_ptr->driver_parameter_ptr);
            }

            return false;
        }
    }

    parameter_ptr->is_set = true;
    *parameter_ptr->value_ptr = *value_ptr;

    return true;
}

union jackctl_parameter_value jackctl_parameter_get_default_value(jackctl_parameter_t *parameter_ptr)
{
    return *(parameter_ptr->default_value_ptr);
}

// Internals clients

const JSList * jackctl_server_get_internals_list(jackctl_server_t *server_ptr)
{
    return server_ptr->internals;
}

const char * jackctl_internal_get_name(jackctl_internal_t *internal_ptr)
{
    return internal_ptr->desc_ptr->name;
}

const JSList * jackctl_internal_get_parameters(jackctl_internal_t *internal_ptr)
{
    return internal_ptr->parameters;
}

bool jackctl_server_load_internal(
    jackctl_server_t * server_ptr,
    jackctl_internal_t * internal)
{
#if 0
    int status;
    if (server_ptr->engine != NULL) {
        server_ptr->engine->InternalClientLoad(internal->desc_ptr->name, internal->desc_ptr->name, internal->set_parameters, JackNullOption, &internal->refnum, -1, &status);
        return (internal->refnum > 0);
    } else {
        return false;
    }
#endif
}

bool jackctl_server_unload_internal(
    jackctl_server_t * server_ptr,
    jackctl_internal_t * internal)
{
#if 0
    int status;
    if (server_ptr->engine != NULL && internal->refnum > 0) {
        return ((server_ptr->engine->GetEngine()->InternalClientUnload(internal->refnum, &status)) == 0);
    } else {
        return false;
    }
#endif
}

bool jackctl_server_add_slave(jackctl_server_t * server_ptr, jackctl_driver_t * driver_ptr)
{
#if 0
    if (server_ptr->engine != NULL) {
        driver_ptr->info = server_ptr->engine->AddSlave(driver_ptr->desc_ptr, driver_ptr->set_parameters);
        return (driver_ptr->info != 0);
    } else {
        return false;
    }
#endif
}

bool jackctl_server_remove_slave(jackctl_server_t * server_ptr, jackctl_driver_t * driver_ptr)
{
#if 0
    if (server_ptr->engine != NULL) {
        server_ptr->engine->RemoveSlave(driver_ptr->info);
        delete driver_ptr->info;
        return true;
    } else {
        return false;
    }
#endif
}

bool jackctl_server_switch_master(jackctl_server_t * server_ptr, jackctl_driver_t * driver_ptr)
{
    jack_driver_t *old_driver;

    if (server_ptr->engine == NULL)
	    goto fail_nostart;

    old_driver = server_ptr->engine->driver;

    if (old_driver)
    {
	    old_driver->stop (old_driver );
	    old_driver->detach (old_driver, server_ptr->engine);

	    pthread_mutex_lock (&server_ptr->engine->request_lock);
	    jack_lock_graph (server_ptr->engine);
	    jack_remove_client (server_ptr->engine, old_driver->internal_client);
	    jack_unlock_graph (server_ptr->engine);
	    pthread_mutex_unlock (&server_ptr->engine->request_lock);

	    jack_stop_watchdog (server_ptr->engine);
	    server_ptr->engine->driver = NULL;

	    jack_driver_unload (old_driver);
    }

    if (jack_engine_load_driver (server_ptr->engine, driver_ptr->desc_ptr, driver_ptr->set_parameters))
    {
	    jack_error ("cannot load driver module %s", driver_ptr->desc_ptr->name);
	    goto fail_nodriver;
    }


    if (server_ptr->engine->driver->start (server_ptr->engine->driver) != 0) {
	    jack_error ("cannot start driver");
	    goto fail_nostart;
    }

    return true;

fail_nodriver:
    jack_error ("could not initialise new driver, leaving without driver");

fail_nostart:
    return false;
}

