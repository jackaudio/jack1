#!/usr/bin/env python

import getopt

def my_getopt(args, shortopts, longopts = []):
    """getopt(args, options[, long_options]) -> opts, args

    Parses command line options and parameter list.  args is the
    argument list to be parsed, without the leading reference to the
    running program.  Typically, this means "sys.argv[1:]".  shortopts
    is the string of option letters that the script wants to
    recognize, with options that require an argument followed by a
    colon (i.e., the same format that Unix getopt() uses).  If
    specified, longopts is a list of strings with the names of the
    long options which should be supported.  The leading '--'
    characters should not be included in the option name.  Options
    which require an argument should be followed by an equal sign
    ('=').

    The return value consists of two elements: the first is a list of
    (option, value) pairs; the second is the list of program arguments
    left after the option list was stripped (this is a trailing slice
    of the first argument).  Each option-and-value pair returned has
    the option as its first element, prefixed with a hyphen (e.g.,
    '-x'), and the option argument as its second element, or an empty
    string if the option has no argument.  The options occur in the
    list in the same order in which they were found, thus allowing
    multiple occurrences.  Long and short options may be mixed.

    """

    opts = []
    if type(longopts) == type(""):
        longopts = [longopts]
    else:
        longopts = list(longopts)
    if args and args[0].startswith('-') and args[0] != '-':
        if args[0] == '--':
            args = args[1:]
        if args[0].startswith('--'):
            opts, args = getopt.do_longs(opts, args[0][2:], longopts, args[1:])
        else:
            opts, args = getopt.do_shorts(opts, args[0][1:], shortopts, args[1:])

	return opts, args
    else:
	return None, args

