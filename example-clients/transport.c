/*
 *  transport.c -- JACK transport master example client.
 *
 *  Copyright (C) 2003 Jack O'Quin.
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <jack/jack.h>
#include <jack/transport.h>

char *package;				/* program name */
int done = 0;

jack_client_t *client;
jack_transport_info_t tinfo;		/* multi-threaded access */


/* JACK process() handler.
 *
 * Runs in a separate realtime thread.  Must not wait.
 */
int process(jack_nframes_t nframes, void *arg)
{
    jack_set_transport_info(client, &tinfo);

    /* frame number for next cycle */
    if (tinfo.transport_state != JackTransportStopped) {
 	tinfo.frame += nframes;

	/* When looping, adjust the frame number periodically.  Make
	 * sure improper loop limits don't lock up the system in an
	 * infinite while(). */
        if ((tinfo.transport_state == JackTransportLooping) &&
	    (tinfo.loop_end > tinfo.loop_start)) {
            while (tinfo.frame >= tinfo.loop_end)
                tinfo.frame -= (tinfo.loop_end - tinfo.loop_start);
        }
    }

    return 0;      
}

void jack_shutdown(void *arg)
{
    exit(1);
}

void signal_handler(int sig)
{
    jack_client_close(client);
    fprintf(stderr, "signal received, exiting ...\n");
    exit(0);
}


/* command functions in alphabetical order */

void com_exit(char *arg)
{
    done = 1;
}

void com_help(char *);			/* forward declaration */

void com_loop(char *arg)
{
    tinfo.transport_state = JackTransportLooping;
}

void com_play(char *arg)
{
    tinfo.transport_state = JackTransportRolling;
}

void com_rewind(char *arg)
{
    tinfo.transport_state = JackTransportStopped;
    tinfo.frame = 0;
}

void com_stop(char *arg)
{
    tinfo.transport_state = JackTransportStopped;
}


/* Command parsing based on GNU readline info examples. */

typedef void cmd_function_t(char *);	/* command function type */

/* Transport command table. */
typedef struct {
    char *name;			/* User printable name of the function. */
    cmd_function_t *func;	/* Function to call to do the job. */
    char *doc;			/* Documentation for this function.  */
} command_t;

/* command list, must be in order */
command_t commands[] = {
    { "exit",	com_exit,	"Exit transport program" },
    { "help",	com_help,	"Display help text" },
    { "loop",	com_loop,	"Start transport looping" },
    { "play",	com_play,	"Start transport rolling" },
    { "quit",	com_exit,	"Synonym for `exit'"},
    { "rewind",	com_rewind,	"Reset transport position to beginning" },
    { "stop",	com_stop,	"Stop transport" },
    { "?",	com_help,	"Synonym for `help'" },
    { (char *)NULL, (cmd_function_t *)NULL, (char *)NULL }
};
     
command_t *find_command(char *name)
{
    register int i;
    size_t namelen;

    if ((name == NULL) || (*name == '\0'))
	return ((command_t *)NULL);

    namelen = strlen(name);
    for (i = 0; commands[i].name; i++)
	if (strncmp(name, commands[i].name, namelen) == 0) {

	    /* make sure the match is unique */
	    if ((commands[i+1].name) &&
		(strncmp(name, commands[i+1].name, namelen) == 0))
		return ((command_t *)NULL);
	    else
		return (&commands[i]);
	}
     
    return ((command_t *)NULL);
}

void com_help(char *arg)
{
    register int i;
    command_t *cmd;

    if (!*arg) {
	/* print help for all commands */
	for (i = 0; commands[i].name; i++) {
	    printf("%s\t\t%s.\n", commands[i].name, commands[i].doc);
	}

    } else if ((cmd = find_command(arg))) {
	printf("%s\t\t%s.\n", cmd->name, cmd->doc);

    } else {
	int printed = 0;

	printf("No `%s' command.  Valid command names are:\n", arg);

	for (i = 0; commands[i].name; i++) {
	    /* Print in six columns. */
	    if (printed == 6) {
		printed = 0;
		printf ("\n");
	    }

	    printf ("%s\t", commands[i].name);
	    printed++;
	}

	printf("\n\nTry `help [command]\' for more information.\n");
    }
}

void execute_command(char *line)
{
    register int i;
    command_t *command;
    char *word;
     
    /* Isolate the command word. */
    i = 0;
    while (line[i] && whitespace(line[i]))
	i++;
    word = line + i;
     
    while (line[i] && !whitespace(line[i]))
	i++;
     
    if (line[i])
	line[i++] = '\0';
     
    command = find_command(word);
     
    if (!command) {
	fprintf(stderr, "%s: No such command.  There is `help\'.\n", word);
	return;
    }
     
    /* Get argument to command, if any. */
    while (whitespace(line[i]))
	i++;
     
    word = line + i;
     
    /* invoke the command function. */
    (*command->func)(word);
}


/* Strip whitespace from the start and end of string. */
char *stripwhite(char *string)
{
    register char *s, *t;

    s = string;
    while (whitespace(*s))
	s++;

    if (*s == '\0')
	return s;
     
    t = s + strlen (s) - 1;
    while (t > s && whitespace(*t))
	t--;
    *++t = '\0';
     
    return s;
}
     
char *dupstr(char *s)
{
    char *r = malloc(strlen(s) + 1);
    strcpy(r, s);
    return r;
}
     
/* Readline generator function for command completion. */
char *command_generator (const char *text, int state)
{
    static int list_index, len;
    char *name;
     
    /* If this is a new word to complete, initialize now.  This
       includes saving the length of TEXT for efficiency, and
       initializing the index variable to 0. */
    if (!state) {
	list_index = 0;
	len = strlen (text);
    }
     
    /* Return the next name which partially matches from the
       command list. */
    while ((name = commands[list_index].name)) {
	list_index++;
     
	if (strncmp(name, text, len) == 0)
	    return dupstr(name);
    }
     
    return (char *) NULL;		/* No names matched. */
}

void command_loop()
{
    char *line, *cmd;
    char prompt[32];

    snprintf(prompt, sizeof(prompt), "%s> ", package);

    /* Allow conditional parsing of the ~/.inputrc file. */
    rl_readline_name = package;
     
    /* Define a custom completion function. */
    rl_completion_entry_function = command_generator;

    /* Read and execute commands until the user quits. */
    while (!done) {

	line = readline(prompt);
     
	if (line == NULL) {		/* EOF? */
	    printf("\n");		/* close out prompt */
	    done = 1;
	    break;
	}
     
	/* Remove leading and trailing whitespace from the line. */
	cmd = stripwhite(line);

	/* If anything left, add to history and execute it. */
	if (*cmd)
	{
	    add_history(cmd);
	    execute_command(cmd);
	}
     
	free(line);			/* realine() called malloc() */
    }
}

void initialize_transport()
{
    /* must run before jack_activate */
    tinfo.loop_start = 0;		/* default loop is one second */
    tinfo.loop_end = jack_get_sample_rate(client);
    tinfo.valid = JackTransportState|JackTransportPosition|JackTransportLoop;
    com_rewind(NULL);
}


int main(int argc, char *argv[])
{
    /* basename $0 */
    package = strrchr(argv[0], '/');
    if (package == 0)
	package = argv[0];
    else
	package++;

    /* become a new client of the JACK server */
    if ((client = jack_client_new(package)) == 0) {
	fprintf(stderr, "jack server not running?\n");
	return 1;
    }

    signal(SIGQUIT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGINT, signal_handler);

    if (jack_engine_takeover_timebase(client) != 0) {
	fprintf(stderr, "Unable to take over timebase.\n");
	fprintf(stderr, "Is another transport master already running?\n");
	return 1;
    }

    jack_set_process_callback(client, process, 0);
    jack_on_shutdown(client, jack_shutdown, 0);

    initialize_transport();

    if (jack_activate(client)) {
	fprintf(stderr, "cannot activate client");
	return 1;
    }

    /* execute commands until done */
    command_loop();

    jack_client_close(client);
    exit(0);
}
