
from xml.dom.minidom import getDOMImplementation, parseString, Element

impl = getDOMImplementation()

class SessionDom( object ):
    def __init__( self, filename=None ):
	if filename:
	    self.dom = parseString( filename )
	else:
	    self.dom = impl.createDocument(None,"jacksession",None)
    
    def add_client( self, client ):
	cl_elem = Element( "jackclient" )
	cl_elem.setAttribute( "cmdline", client.get_commandline() )
	cl_elem.setAttribute( "jackname", client.name )

	for p in client.ports:
	    po_elem = Element( "port" )
	    po_elem.setAttribute( "name", p.name )
	    po_elem.setAttribute( "shortname", p.portname )
	    
	    for c in p.get_connections():
		c_elem = Element( "conn" )
		c_elem.setAttribute( "dst", c )

		po_elem.appendChild( c_elem )

	    cl_elem.appendChild( po_elem )
	
	self.dom.documentElement.appendChild( cl_elem )

    def get_xml(self):
	return self.dom.toxml()

    def get_client_names(self):
	doc = self.dom.documentElement
	for c in doc.getElementsByTagName( "jackclient" ):
	    yield c.getAttribute( "jackname" ).value

    def get_port_names(self):
	doc = self.dom.documentElement
	for c in doc.getElementsByTagName( "port" ):
	    yield c.getAttribute( "name" ).value

    def get_connections_for_port( self, portname ):
	doc = self.dom.documentElement
	for c in doc.getElementsByTagName( "port" ):
	    if c.getAttribute( "name" ).value == portname:
		for i in c.getElementsByTagName( "conn" ):
		    yield i.getAttribute( "dst" ).value

	

	








