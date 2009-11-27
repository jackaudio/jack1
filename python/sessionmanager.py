#!/usr/bin/env python

import libjack
import state
import subprocess
from optparse import OptionParser
from ConfigParser import SafeConfigParser
import os

SESSION_PATH="/home/torbenh/jackSessions/"

defaults = { "jackclientname": "sessionmanager", "sessiondir": "~/jackSessions" }

class SessionManager( object ):
    def __init__( self ):
	self.config = SafeConfigParser( defaults )
	self.config.read( os.path.expanduser( "~/.jacksessionrc" ) )
	self.jackname = self.config.get( "DEFAULT", "jackclientname" )
	self.cl = libjack.JackClient( self.jackname )
	if self.config.has_section( "infra" ):
	    self.infra_clients = {}
	    for inf in self.config.items( "infra" ):
		self.infra_clients[inf[0]] = inf[1]
	else:
	    self.infra_clients = { "a2j": "a2jmidid" }
	self.implicit_clients = [ "system" ]

	self.sessiondir = os.path.expanduser( self.config.get( "DEFAULT", "sessiondir" ) ) + "/" 
	if not os.path.exists( self.sessiondir ):
	    print "Sessiondir %s does not exist. Creating it..."%self.sessiondir
	    os.mkdir( self.sessiondir )


    def load_session( self, name ):
	if not os.path.exists( self.sessiondir+name+"/session.xml" ):
	    print "Session %s does not exist"%name
	    return

	sd = state.SessionDom( self.sessiondir+name+"/session.xml" )

	g=self.cl.get_graph()

	children = []

	for ic in sd.get_infra_clients():
	    if not ic[0] in g.clients.keys():
		children.append( subprocess.Popen( ic[1], shell=True ) )


	print sd.get_client_names()

	g.ensure_clientnames( sd.get_reg_client_names() )

	# get graph again... renaming isnt prefect yet.
	g=self.cl.get_graph()
	# build up list of port connections

	conns = []
	for p in sd.get_port_names():
	    for c in sd.get_connections_for_port( p ):
		conns.append( (p,c) )
	print conns

	# now fire up the processes
	for cname in sd.get_reg_client_names():
	    cmd = sd.get_commandline_for_client( cname )
	    children.append( subprocess.Popen( cmd, shell=True ) )

	avail_ports = g.get_port_list()
	# wait for ports to appear, and connect em.

	while len(conns) > 0:
	    p = self.cl.port_queue.get()
	    print p[0],p[1]
	    pname = libjack.port_name(p[0])
	    for c1 in conns:
		if c1[0]==pname:
		    if c1[1] in avail_ports:
			self.cl.connect( pname, c1[1] )

			conns.remove( c1 )
			if (c1[1],c1[0]) in conns:
			    conns.remove( (c1[1],c1[0]) )

		    break

		if c1[1]==pname:
		    if c1[0] in avail_ports:
			self.cl.connect( pname, c1[0] )

			conns.remove( c1 )
			if (c1[1],c1[0]) in conns:
			    conns.remove( (c1[1],c1[0]) )
		
		    break

	    avail_ports.append( pname )
	    

	print "session restored..."


    def save_session( self, name ): 
	if os.path.exists( self.sessiondir+name ):
	    print "session %s already exists"
	    return
	os.mkdir( self.sessiondir+name )
	g=self.cl.get_graph()
	notify = self.cl.session_save( self.sessiondir+name+"/" )

	for n in notify:
	    c = g.get_client( n.clientname )
	    c.set_commandline( n.commandline )

	sd = state.SessionDom()

	for c in g.clients.values():
	    if c.get_commandline() == "":
		if not c.name in self.infra_clients.keys()+self.implicit_clients:
		    g.remove_client( c.name )
		elif c.name in self.implicit_clients:
		    g.remove_client_only( c.name )
		else:
		    c.set_infra( self.infra_clients[c.name] )


	for i in g.clients.values():
	    sd.add_client(i)

	f = file( self.sessiondir+name+"/session.xml", "w" )
	f.write( sd.get_xml() )
	f.close()

	print sd.get_xml()

    def exit( self ):
	self.cl.close()

oparser = OptionParser()
#oparser.add_option( "--dbus", action="store_true", dest="dbus", default=False,
#		    help="Use DBUS to issue commands to a running instance" )
#oparser.add_option( "--save", action="store_true", dest="save", default=False,
#		    help="Tell SessionManger to save." )
oparser.add_option( "--saveas", action="store", type="string", dest="saveas",
		    help="Save Session As <name>" )

#oparser.add_option( "--quit", action="store_true", dest="quit", default=False,
#		    help="Tell SessionManager to Save And Quit" )

#oparser.add_option( "--quitas", action="store", dest="quitas", type="string",
#		    help="SaveAs And Quit" )
oparser.add_option( "--load", action="store", dest="load", type="string",
		    help="Load Session with <name>" )

(opt,args) = oparser.parse_args()

sm = SessionManager()

if opt.saveas:
    sm.save_session( opt.saveas )

if opt.load:
    sm.load_session( opt.load )

sm.exit()


