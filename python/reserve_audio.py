
import dbus.service
import gobject
import dbus.mainloop.glib

rr = None

class reservation_t( dbus.service.Object ):
    def __init__( self, device_name, prio, override_cb=None ):

        self.dev_name    = device_name
	self.prio        = prio
	self.override_cb = override_cb

	self.bus = dbus.SessionBus()

        dbus.service.Object.__init__( self, None, 
                "/org/freedesktop/ReserveDevice1/" + self.dev_name,
                dbus.service.BusName( "org.freedesktop.ReserveDevice1." + self.dev_name, bus=self.bus, allow_replacement=True, replace_existing=True, do_not_queue=True ) )

    @dbus.service.method( dbus_interface="org.freedesktop.ReserveDevice1", in_signature="i", out_signature="b" )
    def RequestRelease( self, prio ):
	if prio < self.prio:
	    return False

	if self.override_cb:
	    if self.override_cb( self.device_name ):
		self.connection.release_name( 'org.freedesktop.ReserveDevice1.' + self.dev_name )
		return True

	return False



def reserve_dev( dev_name, prio, override_cb ):
    global rr
    try:
	session_bus = dbus.SessionBus()
    except Exception:
	return

    try:
	r_proxy = session_bus.get_object( "org.freedesktop.ReserveDevice1." + dev_name, "/org/freedesktop/ReserveDevice1/" + dev_name )
	r_iface = dbus.Interface( r_proxy, "org.freedesktop.ReserveDevice1" )
    except Exception:
	rr = reservation_t( dev_name, prio, override_cb )
	return

    if not r_iface.RequestRelease( prio ):
	raise Exception

    rr = reservation_t( dev_name, prio, override_cb )



dbus.mainloop.glib.threads_init() 
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)

def run_main():
    loop = gobject.MainLoop()
    loop.run()


