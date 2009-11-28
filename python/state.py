
from xml.dom.minidom import getDOMImplementation, parse, Element
import string

impl = getDOMImplementation()

class SessionDom( object ):
    def __init__( self, filename=None ):
	if filename:
	    self.dom = parse( filename )
	else:
	    self.dom = impl.createDocument(None,"jacksession",None)
    
    def add_client( self, client ):
	cl_elem = Element( "jackclient" )
	cl_elem.setAttribute( "cmdline", client.get_commandline() )
	cl_elem.setAttribute( "jackname", client.name )
	if client.get_uuid():
	    cl_elem.setAttribute( "uuid", client.get_uuid() )
	if client.isinfra:
	    cl_elem.setAttribute( "infra", "True" )
	else:
	    cl_elem.setAttribute( "infra", "False" )

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
	return self.dom.toprettyxml()

    def get_client_names(self):
	retval = []
	doc = self.dom.documentElement
	for c in doc.getElementsByTagName( "jackclient" ):
	    retval.append( c.getAttribute( "jackname" ) )

	return retval

    def get_reg_client_names(self):
	retval = []
	doc = self.dom.documentElement
	for c in doc.getElementsByTagName( "jackclient" ):
	    if c.getAttribute( "infra" ) != "True":
		retval.append( c.getAttribute( "jackname" ) )

	return retval

    def get_infra_clients(self):
	retval = []
	doc = self.dom.documentElement
	for c in doc.getElementsByTagName( "jackclient" ):
	    if c.getAttribute( "infra" ) == "True":
		retval.append( (c.getAttribute( "jackname" ), c.getAttribute( "cmdline" ) ) )

	return retval
    def get_port_names(self):
	retval = []
	doc = self.dom.documentElement
	for c in doc.getElementsByTagName( "port" ):
	    retval.append( c.getAttribute( "name" ) )
	return retval

    def get_connections_for_port( self, portname ):
	retval = []
	doc = self.dom.documentElement
	for c in doc.getElementsByTagName( "port" ):
	    if c.getAttribute( "name" ) == portname:
		for i in c.getElementsByTagName( "conn" ):
		    retval.append( i.getAttribute( "dst" ) )
	return retval

    def get_commandline_for_client( self, name ):
	doc = self.dom.documentElement
	for c in doc.getElementsByTagName( "jackclient" ):
	    if c.getAttribute( "jackname" ) == name:
		return c.getAttribute( "cmdline" )

    def get_uuid_client_pairs( self ):
	retval = []
	doc = self.dom.documentElement
	for c in doc.getElementsByTagName( "jackclient" ):
	    if c.getAttribute( "infra" ) != "True":
		retval.append( (c.getAttribute( "uuid" ), c.getAttribute( "jackname" )) )

	return retval

    def renameclient( self, celem, newname ):
	doc = self.dom.documentElement
	celem.setAttribute( "jackname", newname )
	for pelem in celem.getElementsByTagName( "port" ):
	    old_pname = pelem.getAttribute( "name" )
	    pname_split = old_pname.split(":")
	    pname_split[0] = newname
	    new_pname = string.join( pname_split, ":" )
	    pelem.setAttribute( "name", new_pname )
	    for dst in doc.getElementsByTagName( "conn" ):
		if dst.getAttribute( "dst" ) == old_pname:
		    dst.setAttribute( "dst", new_pname )


	    
    def fixup_client_names( self, graph ):
	doc = self.dom.documentElement
	for c in doc.getElementsByTagName( "jackclient" ):
	    if c.getAttribute( "infra" ) == "True":
		continue
	    cname = c.getAttribute( "jackname" )
	    if cname in graph.get_taken_names():
		free_name = graph.get_free_name( cname, self.get_reg_client_names() )
		print "name taken %s rename to %s"%(cname, free_name )
		self.renameclient( c, free_name )
		

	

	








