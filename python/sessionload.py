

import libjack
import state
import subprocess

SESSION_PATH="/home/torbenh/jackSessions/"
implicit_clients = ["system"]
infra_clients = ["system"]

sd = state.SessionDom( SESSION_PATH+"session.xml" )

cl=libjack.JackClient("bla5")
g=cl.get_graph()

print sd.get_client_names()

g.ensure_clientnames( sd.get_client_names() )

# get graph again... renaming isnt prefect yet.
g=cl.get_graph()
# build up list of port connections

conns = []
for p in sd.get_port_names():
    for c in sd.get_connections_for_port( p ):
	conns.append( (p,c) )
print conns

# now fire up the processes
children = []
for cname in sd.get_client_names():
    cmd = sd.get_commandline_for_client( cname )
    children.append( subprocess.Popen( cmd, shell=True ) )

avail_ports = g.get_port_list()
# wait for ports to appear, and connect em.

while len(conns) > 0:
    p = cl.port_queue.get()
    print p[0],p[1]
    pname = libjack.port_name(p[0])
    for c1 in conns:
	if c1[0]==pname:
	    if c1[1] in avail_ports:
		cl.connect( pname, c1[1] )

		conns.remove( c1 )
		if (c1[1],c1[0]) in conns:
		    conns.remove( (c1[1],c1[0]) )

	    break

	if c1[1]==pname:
	    if c1[0] in avail_ports:
		cl.connect( pname, c1[0] )

		conns.remove( c1 )
		if (c1[1],c1[0]) in conns:
		    conns.remove( (c1[1],c1[0]) )
	
	    break

    avail_ports.append( pname )
    

print "session restored... exiting"
cl.close()

