
from ctypes import *
import string
from Queue import Queue

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

client_open = libjack.jack_client_open
client_open.argtypes = [ c_char_p, c_uint, POINTER( c_uint ) ]
client_open.restype = client_p

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
    _fields_ = [ ("uuid", 16*c_char ), ("clientname", 33*c_char), ("command", 256*c_char ) ]

session_notify = libjack.jack_session_notify
session_notify.argtypes = [ client_p, c_uint, c_char_p ]
session_notify.restype = POINTER( jack_session_command_t )

get_client_name_by_uuid = libjack.jack_get_client_name_by_uuid
get_client_name_by_uuid.argtypes = [ client_p, c_char_p ]
get_client_name_by_uuid.restype = c_char_p

get_cookie_by_uuid = libjack.jack_get_cookie_by_uuid
get_cookie_by_uuid.argtypes = [ client_p, c_char_p, c_char_p ]
get_cookie_by_uuid.restype = c_char_p 

connect = libjack.jack_connect
connect.argtypes = [ client_p, c_char_p, c_char_p ]
connect.restype = c_int

disconnect = libjack.jack_disconnect
disconnect.argtypes = [ client_p, c_char_p, c_char_p ]
disconnect.restype = c_int

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

JackSessionSave = 1
JackSessionQuit = 2



class Port( object ):
    def __init__( self, client, name ):
	self.client = client
	self.name = name
	self.portname = name.split(':')[1]
	self.port_p = port_by_name( self.client, name )
	self.conns = self.get_live_connections()

    def get_connections( self ):
	return self.conns

    def get_live_connections( self ):
	conns = port_get_all_connections( self.client, self.port_p )
	if not conns:
	    return []

	i=0
	retval = []
	while conns[i]:
	    retval.append( conns[i] )
	    i+=1
	jack_free(conns)

	return retval

    def connect( self, other ):
	connect( self.client, self.name, other )

    def disconnect( self, other ):
	disconnect( self.client, self.name, other )

    def is_input( self ):
	return (port_flags( self.port_p ) & JackPortIsInput) != 0

    def is_output( self ):
	return (port_flags( self.port_p ) & JackPortIsOutput) != 0

    def is_audio( self ):
	return (port_type( self.port_p ) == JACK_DEFAULT_AUDIO_TYPE)

    def is_midi( self ):
	return (port_type( self.port_p ) == JACK_DEFAULT_MIDI_TYPE)


class Client( object ):
    def __init__( self, client, name ):
	self.client = client
	self.name = name
	self.ports = []
	self.commandline = None
	self.isinfra = False

    def get_commandline( self ):
	if self.commandline:
	    return self.commandline
	else:
	    return ""

    def set_commandline( self, cmdline ):
	self.commandline = cmdline

    def add_port( self, portname ):
	self.ports.append( Port( self.client, portname ) )

    def rename( self, newname ):
	rename_client( self.client, self.name, newname )
	self.ports = []
	ports = get_ports( self.client, newname+":.*", None, 0 )
	self.name = newname

	i=0
	while(ports[i]):
	    self.add_port( ports[i] )
	    i+=1

	jack_free( ports )

    def set_infra( self, cmdline ):
	self.isinfra = True
	self.commandline = cmdline



class JackGraph( object ):
    def __init__( self, client, ports, uuids=[] ):
	self.client = client
	self.clients = {}

	i=0
	while(ports[i]):
	    port_split = ports[i].split(':')
	    if not self.clients.has_key(port_split[0]):
		self.clients[port_split[0]] = Client( client, port_split[0] )

	    self.clients[port_split[0]].add_port( ports[i] )
	    i+=1

    def get_client( self, name ):
	return self.clients[name]

    def get_port_list( self ):
	retval = []
	for c in self.clients.values():
	    for p in c.ports:
		retval.append( p.name )
	return retval



    def check_client_name( self, client ):
	if not client.name in self.reserved_names:
	    return

	oldname = client.name
	cname_split = client.name.split('-')
	if len(cname_split) == 1:
	    cname_prefix = cname_split[0]
	else:
	    cname_prefix = string.join( cname_split[:-1], '-' )

	num = 1
	while ("%s-%d"%(cname_prefix,num)) in (self.clients.keys()+self.reserved_names):
		num+=1

	# XXX: this might still fail due to race. 
	#      also needs to lock 
	newname = "%s-%d"%(cname_prefix,num )

	client.rename( newname )
	del self.clients[oldname]
	self.clients[newname] = client


    def remove_client( self, name ):
	del self.clients[name]
	for c in self.clients.values():
	    for p in c.ports:
		for conn in p.get_connections():
		    if conn.startswith(name+":"):
			p.conns.remove( conn )

    def remove_client_only( self, name ):
	del self.clients[name]

    def ensure_clientnames( self, names ):
	self.reserved_names = names
	for c in self.clients.values():
	    self.check_client_name( c )


class NotifyReply(object):
    def __init__( self, uuid, clientname, commandline ):
	self.uuid = uuid
	self.clientname = clientname
	self.commandline = commandline


class JackClient(object):
    def __init__( self, name ):
	self.client = client_open( name, 0, None )
	if not self.client:
	    raise Exception( "got no client name" )
	self.reg_cb = PortRegistrationCallback( self.port_registration_cb )
	set_port_registration_callback( self.client, self.reg_cb, None )
	self.port_queue = Queue()

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
	port_p = port_by_id( self.client, port_id )
	self.port_queue.put( (port_p,reg) )


    def session_save( self, path ):
	commands = session_notify( self.client, JackSessionSave, path )
	i=0
	retval = []
	while( commands[i].uuid != "" ):
	    retval.append( NotifyReply( commands[i].uuid, commands[i].clientname, commands[i].command ) )
	    i+=1
	
	jack_free( commands )

	return retval

    def connect( self, a, b ):
	portA_p = port_by_name( self.client, a )
	
	if( port_flags( portA_p ) & JackPortIsOutput ):
	    connect( self.client, a, b )
	else:
	    connect( self.client, b, a )






	    
	    


