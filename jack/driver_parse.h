/* -*- mode: c; c-file-style: "linux"; -*- */
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
	char arg_default[JACK_DRIVER_PARAM_STRING_MAX + 1];

	for (i = 0; i < desc->nparams; i++) {
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

		fprintf (file, "\t-%c, --%s \t%s (default: %s)\n",
			 desc->params[i].character,
			 desc->params[i].name,
			 desc->params[i].short_desc,
			 arg_default);
	}
}

static void
jack_print_driver_param_usage (jack_driver_desc_t * desc, unsigned long param, FILE *file)
{
	fprintf (file, "Usage information for the '%s' parameter for driver '%s':\n",
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

	if (argc <= 1) {
		*param_ptr = NULL;
		return 0;
	}

	/* check for help */
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

		printf ("Parameters for driver '%s' (all parameters are optional):\n", desc->name);
		jack_print_driver_options (desc, stdout);
		return 1;
	}


	/* set up the stuff for getopt */
	options = calloc (desc->nparams*3 + 1, sizeof (char));
	long_options = calloc (desc->nparams + 1, sizeof (struct option));

	options_ptr = options;
	for (i = 0; i < desc->nparams; i++) {
		sprintf (options_ptr, "%c::", desc->params[i].character);
		options_ptr += 3;
		
		long_options[i].name    = desc->params[i].name;
		long_options[i].flag    = NULL;
		long_options[i].val     = desc->params[i].character;
		long_options[i].has_arg = optional_argument;
	}

	/* create the params */
	optind = 0;
	opterr = 0;
	while ((opt = getopt_long(argc, argv, options, long_options, NULL)) != -1) {

		if (opt == ':' || opt == '?') {
			if (opt == ':') {
				fprintf (stderr, "Missing option to argument '%c'\n", optopt);
			} else {
				fprintf (stderr, "Unknownage with option '%c'\n", optopt);
			}

			fprintf (stderr, "Options for driver '%s':\n", desc->name);
			jack_print_driver_options (desc, stderr);
			exit (1);
		}

		for (param_index = 0; param_index < desc->nparams; param_index++) {
			if (opt == desc->params[param_index].character) {
				break;
			}
		}

		driver_param = calloc (1, sizeof (jack_driver_param_t));

		driver_param->character = desc->params[param_index].character;

		if (!optarg && optind < argc &&
		    strlen(argv[optind]) &&
		    argv[optind][0] != '-') {
			optarg = argv[optind];
		}

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

				if (strcasecmp ("false",  optarg) == 0 ||
				    strcasecmp ("off",    optarg) == 0 ||
				    strcasecmp ("no",     optarg) == 0 ||
				    strcasecmp ("0",      optarg) == 0 ||
				    strcasecmp ("(null)", optarg) == 0   ) {

					driver_param->value.i = FALSE;

				} else {

					driver_param->value.i = TRUE;

				}
				break;
			}
                } else {
			if (desc->params[param_index].type == JackDriverParamBool) {
				driver_param->value.i = TRUE;
			} else {
				driver_param->value = desc->params[param_index].value;
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


