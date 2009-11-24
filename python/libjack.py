
from ctypes import *

class jack_client_t(Structure):
    pass

class jack_port_t(Structure):
    pass

client_p = POINTER(jack_client_t)
port_p = POINTER(jack_port_t)

libjack = cdll.LoadLibrary( "libjack.so" )
client_new = libjack.jack_client_new
client_new.argtypes = [ c_char_p ]
client_new.restype = client_p

client_close = libjack.jack_client_close
client_close.argtypes = [ client_p ]
client_close.restype = None

activate = libjack.jack_activate
activate.argtypes = [ client_p ]
activate.restype = None

deactivate = libjack.jack_deactivate
deactivate.argtypes = [ client_p ]
deactivate.restype = None

get_ports = libjack.jack_get_ports
get_ports.argtypes = [ client_p, c_char_p, c_char_p, c_ulong ]
get_ports.restype = POINTER( c_char_p )

port_by_name = libjack.jack_port_by_name
port_by_name.argtypes = [ client_p, c_char_p ]
port_by_name.restype = port_p

port_get_all_connections = libjack.jack_port_get_all_connections
port_get_all_connections.argtypes = [ client_p, port_p ]
port_get_all_connections.restype = POINTER( c_char_p )

jack_free = libjack.jack_free 
jack_free.argtypes = [ c_void_p ]
jack_free.restype = None

rename_client = libjack.jack_rename_client
rename_client.argtypes = [ client_p, c_char_p, c_char_p ]
rename_client.restype = c_int 

class jack_session_command_t( Structure ):
    _fields_ = [ ("uuid", 16*c_char ), ("command", 256*c_char ) ]

session_notify = libjack.jack_session_notify
session_notify.argtypes = [ client_p, c_uint, c_char_p ]
session_notify.restype = POINTER( jack_session_command_t )

get_client_name_by_uuid = libjack.jack_get_client_name_by_uuid
get_client_name_by_uuid.argtypes = [ client_p, c_char_p ]
get_client_name_by_uuid.restype = c_char_p

get_cookie_by_uuid = libjack.jack_get_cookie_by_uuid
get_cookie_by_uuid.argtypes = [ client_p, c_char_p, c_char_p ]
get_cookie_by_uuid.restype = c_char_p 


PortRegistrationCallback = CFUNCTYPE( None, c_uint, c_int, c_void_p )

set_port_registration_callback = libjack.jack_set_port_registration_callback
set_port_registration_callback.argtypes = [ client_p, PortRegistrationCallback, c_void_p ]
set_port_registration_callback.restype = c_int

port_by_id = libjack.jack_port_by_id
port_by_id.argtypes = [ client_p, c_uint ]
port_by_id.restype = port_p

port_name = libjack.jack_port_name
port_name.argtypes = [ port_p ]
port_name.restype = c_char_p

port_type = libjack.jack_port_type
port_type.argtypes = [ port_p ]
port_type.restype = c_char_p

port_flags = libjack.jack_port_flags
port_flags.argtypes = [ port_p ]
port_flags.restype = c_int

JACK_DEFAULT_AUDIO_TYPE="32 bit float mono audio"
JACK_DEFAULT_MIDI_TYPE="8 bit raw midi"

JackPortIsInput = 0x1
JackPortIsOutput = 0x2
JackPortIsPhysical = 0x4
JackPortCanMonitor = 0x8
JackPortIsTerminal = 0x10


class Port( object ):
    def __init__( self, client, name ):
	self.client = client
	self.name = name
	self.portname = name.split(':')[1]
	self.port_p = port_by_name( self.client, name )

    def get_connections( self ):
	conns = port_get_all_connections( self.client, self.port_p )
	i=0
	retval = []
	while conns[i]:
	    retval.append( conns[i] )
	    i+=1
	jack_free(conns)
	

	return retval



class Client( object ):
    def __init__( self, client, name ):
	self.client = client
	self.name = name
	self.ports = []
    def add_port( self, portname ):
	self.ports.append( Port( self.client, portname ) )


class JackGraph( object ):
    def __init__( self, client, ports, uuids=[] ):
	self.client = client
	self.clients = {}
	self.uuid_to_name = {}
	self.name_to_uuid = {}
	for uid in uuids:
	    name = get_client_name_by_uuid( self.client, uid )
	    if name:
		self.uuid_to_name[uid] = name
		self.name_to_uuid[name] = uid
		jack_free( name )

	i=0
	while(ports[i]):
	    port_split = ports[i].split(':')
	    if not self.clients.has_key(port_split[0]):
		self.clients[port_split[0]] = Client( client, port_split[0] )

	    self.clients[port_split[0]].add_port( ports[i] )
	    i+=1

    def get_client( self, name ):
	return self.clients[name]

    def get_client_by_uuid( self, uuid ):
	return self.clients[self.uuid_to_name[uuid]]



class JackClient(object):
    def __init__( self, name ):
	self.client = client_new( name )
	self.reg_cb = PortRegistrationCallback( self.port_registration_cb )
	set_port_registration_callback( self.client, self.reg_cb, None )

	activate( self.client )

    def close( self ):
	client_close( self.client )

    def get_graph( self ):
	ports = get_ports( self.client, None, None, 0 )
	retval = JackGraph( self.client, ports )
	jack_free( ports )
	return retval

    def rename_client( self, old, new ):
	if rename_client( self.client, old, new ):
	    raise Exception

    def port_registration_cb( self, port_id, reg, arg ):
	port_t = port_by_id( self.client, port_id )
	print port_name( port_t )
	print port_type( port_t )
	print reg



	    
	    


