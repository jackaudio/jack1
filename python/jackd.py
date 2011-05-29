#!/usr/bin/env python

import sys
from mygetopt import my_getopt
import jackctl

argv = sys.argv[1:]



def server_parse_ags( srv, argv ):
    shortopts = ""
    longopts  = []
    shortmap  = {}
    driver = None

    for param in srv.params:
	p = srv.params[param]
	shortopts += p.id
	if p.param_type != 5:
	    shortopts += ":"
	    longopts.append (p.name + "=")
	else:
	    longopts.append (p.name)

	shortmap[p.id] = p

    print shortopts

    while not driver:
	opts, argv = my_getopt( argv, shortopts+"d:" )
	if not opts:
	    break
	for opt,optarg in opts:
	    if opt == "-d":
		driver = srv.drivers[optarg]
	    elif opt.startswith("--"):
		pass
	    elif opt.startswith("-"):
		p = shortmap[opt[1]]
		if p.param_type == 5:
		    p.value = True
		else:
		    p.value = optarg

    return driver, argv

def driver_parse_args( drv, argv ):
    shortopts = ""
    longopts  = []
    shortmap  = {}

    for param in drv.params:
	p = drv.params[param]
	shortopts += p.id
	if p.param_type != 5:
	    shortopts += ":"
	    longopts.append (p.name + "=")
	else:
	    longopts.append (p.name)

	shortmap[p.id] = p

    print shortopts

    while True:
	opts, argv = my_getopt( argv, shortopts+"d:" )
	if not opts:
	    break
	for opt,optarg in opts:
	    if opt.startswith("--"):
		pass
	    elif opt.startswith("-"):
		p = shortmap[opt[1]]
		if p.param_type == 5:
		    p.value = True
		else:
		    p.value = optarg


srv = jackctl.Server()

drv, argv = server_parse_ags( srv, argv )
driver_parse_args( drv, argv )


for p in srv.params.values():
    print p.name, "-> ", p.value

print "----------------"
print "driver ", drv.name

for p in drv.params.values():
    print p.name, "-> ", p.value

srv.start( drv )

while raw_input("hi> ") != "quit":
    pass
