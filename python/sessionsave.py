
import libjack
import state

SESSION_PATH="/home/torbenh/jackSessions/"
implicit_clients = ["system"]
infra_clients = { "a2j": "a2jmidid" }

cl=libjack.JackClient("bla")
notify = cl.session_save( SESSION_PATH )
g=cl.get_graph()

for n in notify:
    c = g.get_client( n.clientname )
    c.set_commandline( n.commandline )

sd = state.SessionDom()

for c in g.clients.values():
    if c.get_commandline() == "":
	if not c.name in infra_clients.keys()+implicit_clients:
	    g.remove_client( c.name )
	elif c.name in implicit_clients:
	    g.remove_client_only( c.name )
	else:
	    c.set_infra( infra_clients[c.name] )


for i in g.clients.values():
    sd.add_client(i)

f = file( SESSION_PATH+"session.xml", "w" )
f.write( sd.get_xml() )
f.close()

print sd.get_xml()

cl.close()
