#!/usr/bin/env python

import sys
from pyjackd.mygetopt import my_getopt
from pyjackd import jackctl
import readline
import time

import reserve_audio

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

def acquire_dev(cardname):
    reserve_audio.reserve_dev(cardname,20,None)
    time.sleep(0.1)
    return True

def release_dev(cardname):
    reserve_audio.rr.unreserve()
    reserve_audio.rr = None

srv = jackctl.Server()
srv.acquire_card_cb = acquire_dev
srv.release_card_cb = release_dev

drv, argv = server_parse_ags( srv, argv )
driver_parse_args( drv, argv )

#for p in srv.params.values():
#    print p.name, "-> ", p.value
#
#print "----------------"
#print "driver ", drv.name
#
#for p in drv.params.values():
#    print p.name, "-> ", p.value

started = srv.start( drv )

if not started:
    print "failed to start with driver " + drv.name
    print "trying to start with dummy driver, switch to the right master yourself"

    started = srv.start( srv.drivers["dummy"] )

    if not started:
	sys.exit(20)

quit = False
while not quit:
    try:
	cmd  = raw_input("jack> ")
    except EOFError:
	break

    cmdv = cmd.split()

    if len(cmdv) == 0:
	continue

    if cmdv[0] == "quit":
	quit = True
    elif cmdv[0] == "switch":
	if len(cmdv) > 1:
	    drv = srv.drivers[cmdv[1]]
	    driver_parse_args( drv, cmdv[2:] )
	    srv.switch_master( drv )

print "\nshutting down"
srv.stop()
