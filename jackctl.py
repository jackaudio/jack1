
from ctypes import *

libj = cdll.LoadLibrary( "libjack.so" )
libjs = cdll.LoadLibrary( "libjackserver.so" )


class jackctl_parameter_value( Union ):
    _fields_ = [ ( "ui", c_uint ),
                 ( "i",  c_int  ),
                 ( "c",  c_char ),
                 ( "ss", c_char * 128 ),
                 ( "b",   c_bool ) ]

    def get_str( self ):
        return buffer(self.ss)

    def set_str( self, sss ):
        self.ss = sss

    str = property( get_str, set_str )

class jackctl_server_t( Structure ):
    pass

class jackctl_driver_t( Structure ):
    pass

class jackctl_internal_t( Structure ):
    pass

class jackctl_parameter_t( Structure ):
    pass

class JSList( Structure ):
    pass

JSList._fields_ = [ ("data", c_void_p), ("next", POINTER(JSList)) ]

class JSIter:
    def __init__(self, ptr, typ=c_void_p):
        self.ptr = ptr
	self.typ = typ

    def __iter__(self):
        return self

    def next( self ):
        if not self.ptr:
            raise StopIteration

        retval = self.ptr.contents.data
        self.ptr = self.ptr.contents.next

	return cast( retval, self.typ )


jackctl_server_start = libjs.jackctl_server_start
jackctl_server_start.argtypes = [ POINTER(jackctl_server_t), POINTER(jackctl_driver_t) ]
jackctl_server_start.restype  = c_bool

jackctl_server_stop = libjs.jackctl_server_stop
jackctl_server_stop.argtypes = [ POINTER(jackctl_server_t) ]
jackctl_server_stop.restype  = c_bool

jackctl_server_create = libjs.jackctl_server_create
jackctl_server_create.argtypes = [ POINTER(jackctl_server_t), POINTER(jackctl_driver_t) ]
jackctl_server_create.restype  = POINTER(jackctl_server_t)

jackctl_server_get_drivers_list = libjs.jackctl_server_get_drivers_list
jackctl_server_get_drivers_list.argtypes = [ POINTER(jackctl_server_t) ]
jackctl_server_get_drivers_list.restype  = POINTER(JSList)

jackctl_server_get_parameters = libjs.jackctl_server_get_parameters
jackctl_server_get_parameters.argtypes = [ POINTER(jackctl_server_t) ]
jackctl_server_get_parameters.restype  = POINTER(JSList)

jackctl_driver_get_parameters = libjs.jackctl_driver_get_parameters
jackctl_driver_get_parameters.argtypes = [ POINTER(jackctl_driver_t) ]
jackctl_driver_get_parameters.restype  = POINTER(JSList)

jackctl_driver_get_name = libjs.jackctl_driver_get_name
jackctl_driver_get_name.argtypes = [ POINTER(jackctl_driver_t) ]
jackctl_driver_get_name.restype  = c_char_p

jackctl_parameter_get_name = libjs.jackctl_parameter_get_name
jackctl_parameter_get_name.argtypes = [ POINTER(jackctl_parameter_t) ]
jackctl_parameter_get_name.restype  = c_char_p

jackctl_parameter_get_short_description = libjs.jackctl_parameter_get_short_description
jackctl_parameter_get_short_description.argtypes = [ POINTER(jackctl_parameter_t) ]
jackctl_parameter_get_short_description.restype  = c_char_p

jackctl_parameter_get_type = libjs.jackctl_parameter_get_type
jackctl_parameter_get_type.argtypes = [ POINTER(jackctl_parameter_t) ]
jackctl_parameter_get_type.restype  = c_uint

jackctl_parameter_set_value = libjs.jackctl_parameter_set_value
jackctl_parameter_set_value.argtypes = [ POINTER(jackctl_parameter_t), POINTER(jackctl_parameter_value) ]
jackctl_parameter_set_value.restype  = c_bool

jackctl_parameter_get_value = libjs.jackctl_parameter_get_value
jackctl_parameter_get_value.argtypes = [ POINTER(jackctl_parameter_t) ]
jackctl_parameter_get_value.restype  = jackctl_parameter_value

jackctl_server_switch_master = libjs.jackctl_server_switch_master
jackctl_server_switch_master.argtypes = [ POINTER(jackctl_server_t), POINTER(jackctl_driver_t) ]
jackctl_server_switch_master.restype  = c_bool


class Parameter(object):
    def __init__( self, param_ptr ):
	self.param_ptr = param_ptr
	self.param_type = jackctl_parameter_get_type( self.param_ptr )

    def get_short_desc( self ):
	return jackctl_parameter_get_short_description( self.param_ptr )

    short_desc = property( get_short_desc )

    def get_name( self ):
	return jackctl_parameter_get_name( self.param_ptr )

    name = property( get_name )

    def set_value( self, val ):
	param_v = jackctl_parameter_value()
	if self.param_type == 1:
	    # int
	    assert( type(val) == int )
	    param_v.i = val
	elif self.param_type == 2:
	    # uint
	    assert( type(val) == int )
	    param_v.ui = val
	elif self.param_type == 3:
	    # char
	    assert( (type(val) == str) and len(val)==1 )
	    param_v.c = val
	elif self.param_type == 4:
	    # string
	    assert( type(val) == str )
	    param_v.str = val
	elif self.param_type == 5:
	    # bool
	    assert( type(val) == bool )
	    param_v.b = val
	    
	jackctl_parameter_set_value( self.param_ptr, pointer(param_v) )

    def get_value( self ):
	param_v = jackctl_parameter_get_value( self.param_ptr )

	if self.param_type == 1:
	    # int
	    return param_v.i
	elif self.param_type == 2:
	    # uint
	    return param_v.ui
	elif self.param_type == 3:
	    # char
	    return param_v.c
	elif self.param_type == 4:
	    # string
	    return param_v.ss.value
	elif self.param_type == 5:
	    # bool
	    return param_v.b

    value = property( get_value, set_value )

class Driver(object):
    def __init__( self, drv_ptr ):
	self.drv_ptr = drv_ptr

	params_jslist = jackctl_driver_get_parameters( self.drv_ptr )

	self.params = {}
	for i in JSIter( params_jslist, POINTER(jackctl_parameter_t) ):
	    self.params[ jackctl_parameter_get_name( i ) ] = Parameter(i)


class Server(object):
    def __init__( self ):
	self.srv_ptr = jackctl_server_create( None, None )

	driver_jslist = jackctl_server_get_drivers_list( self.srv_ptr )

	self.drivers = {}
	for i in JSIter( driver_jslist, POINTER(jackctl_driver_t) ):
	    self.drivers[ jackctl_driver_get_name( i ) ] = Driver(i)

	params_jslist = jackctl_server_get_parameters( self.srv_ptr )

	self.params = {}
	for i in JSIter( params_jslist, POINTER(jackctl_parameter_t) ):
	    self.params[ jackctl_parameter_get_name( i ) ] = Parameter(i)

    def __del__( self ):
	pass

    def start( self, driver ):
	return jackctl_server_start( self.srv_ptr, driver.drv_ptr )

    def switch_master( self, driver ):
	return jackctl_server_switch_master( self.srv_ptr, driver.drv_ptr )

    def stop( self ):
	return jackctl_server_stop( self.srv_ptr )
