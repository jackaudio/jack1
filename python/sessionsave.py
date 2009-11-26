
import libjack
import state

SESSION_PATH="/home/torbenh/jackSessions/"
implicit_clients = ["system"]
infra_clients = ["system"]

cl=libjack.JackClient("bla")
notify = cl.session_save( SESSION_PATH )
g=cl.get_graph()

for n in notify:
    c = g.get_client( n.clientname )
    c.set_commandline( n.commandline )

for c in g.clients.values():
    if c.get_commandline() == "":
	if not c.name in infra_clients:
	    g.remove_client( c.name )
	elif c.name in implicit_clients:
	    g.remove_client_only( c.name )

sd = state.SessionDom()

for i in g.clients.values():
    sd.add_client(i)

f = file( SESSION_PATH+"session.xml", "w" )
f.write( sd.get_xml() )
f.close()

print sd.get_xml()

cl.close()
