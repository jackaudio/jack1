/* -*- mode: c; c-file-style: "bsd"; -*- */
/*
  Copyright (C) 2003 Bob Ham <rah@bash.sh
    
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

*/

#ifndef __jack_driver_parse_h__
#define __jack_driver_parse_h__

#include <jack/jslist.h>
#include <jack/driver_interface.h>

static void
jack_print_driver_options (jack_driver_desc_t * desc, FILE *file)
{
	unsigned long i;
	const char * arg_string = "";
	char arg_default[JACK_DRIVER_PARAM_STRING_MAX + 1];

	for (i = 0; i < desc->nparams; i++) {
		switch (desc->params[i].has_arg) {
		case no_argument:
			arg_string = "\t";
			break;
		case optional_argument:
			arg_string = " [<arg>]";
			break;
		case required_argument:
			arg_string = " <arg>";
			break;
		}

		switch (desc->params[i].type) {
		case JackDriverParamInt:
			sprintf (arg_default, "%" PRId32, desc->params[i].value.i);
			break;
		case JackDriverParamUInt:
			sprintf (arg_default, "%" PRIu32, desc->params[i].value.ui);
			break;
		case JackDriverParamChar:
			sprintf (arg_default, "%c", desc->params[i].value.c);
			break;
		case JackDriverParamString:
			if (desc->params[i].value.str &&
			    strcmp (desc->params[i].value.str, "") != 0)
				sprintf (arg_default, "%s", desc->params[i].value.str);
			else
				sprintf (arg_default, "none");
			break;
		case JackDriverParamBool:
			sprintf (arg_default, "%s", desc->params[i].value.i ? "true" : "false");
			break;
		}

		fprintf (file, "\t-%c, --%s%s\t%s (default: %s)\n",
			 desc->params[i].character,
			 desc->params[i].name,
			 arg_string,
			 desc->params[i].short_desc,
			 arg_default);
	}
}

static void
jack_print_driver_param_usage (jack_driver_desc_t * desc, unsigned long param, FILE *file)
{
	fprintf (file, "Usage information for the '%s' option for driver '%s':\n",
		 desc->params[param].name, desc->name);
 
	fprintf (file, "%s\n", desc->params[param].long_desc);
}


static int
jack_parse_driver_params (jack_driver_desc_t * desc, int argc, char **argv, JSList ** param_ptr)
{
	struct option * long_options;
	char * options, * options_ptr;
	unsigned long i;
	int opt, param_index;
	JSList * params = NULL;
	jack_driver_param_t * driver_param;

	/* check for help */
	if (argc > 1) {
		if (strcmp (argv[1], "-h") == 0 || strcmp (argv[1], "--help") == 0) {
			if (argc > 2) {
				for (i = 0; i < desc->nparams; i++) {
					if (strcmp (desc->params[i].name, argv[2]) == 0) {
						jack_print_driver_param_usage (desc, i, stdout);
						return 1;
					}
				}

				fprintf (stderr, "jackd: unknown option '%s' "
					 "for driver '%s'\n", argv[2],
					 desc->name);
			}

			printf ("Options for driver '%s':\n", desc->name);
			jack_print_driver_options (desc, stdout);
			return 1;
		}
	} else {
		/* save some processing */
		*param_ptr = NULL;
		return 0;
	}


	/* set up the stuff for getopt */
	options = calloc (desc->nparams*3 + 1, sizeof (char));
	options_ptr = options;
	long_options = calloc (desc->nparams + 1, sizeof (struct option));

	for (i = 0; i < desc->nparams; i++) {
		*options_ptr = desc->params[i].character;
		options_ptr++;
		if (desc->params[i].has_arg > 0) {
			*options_ptr = ':';
			options_ptr++;

			if (desc->params[i].has_arg == optional_argument) {
				*options_ptr = ':';
				options_ptr++;
			}
		}

		long_options[i].name    = desc->params[i].name;
		long_options[i].has_arg = desc->params[i].has_arg;
		long_options[i].flag    = NULL;
		long_options[i].val     = desc->params[i].character;
	}

	/* create the params */
	optind = 0;
	opterr = 0;
	while ((opt = getopt_long(argc, argv, options, long_options, NULL)) != -1) {

		if (opt == ':' || opt == '?') {
			if (opt == ':') {
				fprintf (stderr, "Missing option to argument '%c'\n", optopt);
			} else {
				fprintf (stderr, "Unknown option '%c'\n", optopt);
			}

			fprintf (stderr, "Options for driver '%s':\n", desc->name);
			jack_print_driver_options (desc, stderr);
			exit (1);
		}

		for (param_index = 0; desc->nparams; param_index++) {
			if (opt == desc->params[param_index].character)
				break;
		}

		driver_param = calloc (1, sizeof (jack_driver_param_t));

		driver_param->character = desc->params[param_index].character;

		if (optarg) {
			switch (desc->params[param_index].type) {
			case JackDriverParamInt:
				driver_param->value.i = atoi (optarg);
				break;
			case JackDriverParamUInt:
				driver_param->value.ui = strtoul (optarg, NULL, 10);
				break;
			case JackDriverParamChar:
				driver_param->value.c = optarg[0];
				break;
			case JackDriverParamString:
				strncpy (driver_param->value.str, optarg, JACK_DRIVER_PARAM_STRING_MAX);
				break;
			case JackDriverParamBool:
				driver_param->value.i = 1;
				break;
			}
		}

		params = jack_slist_append (params, driver_param);
	}

	free (options);
	free (long_options);

	if (param_ptr)
		*param_ptr = params;

	return 0;
}


#endif /* __jack_driver_parse_h__ */


