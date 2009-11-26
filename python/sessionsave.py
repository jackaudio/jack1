
import libjack
import state


c=libjack.JackClient("bla")
g=c.get_graph()

sd = state.SessionDom()

for i in g.clients.values():
    sd.add_client(i)

print sd.get_xml()

