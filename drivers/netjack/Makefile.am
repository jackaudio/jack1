MAINTAINCLEANFILES = Makefile.in

AM_CFLAGS = $(JACK_CFLAGS) 

if HAVE_SAMPLERATE

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_net.la

jack_net_la_LIBADD = -lsamplerate

jack_net_la_LDFLAGS = -module -avoid-version 
jack_net_la_SOURCES = net_driver.c netjack_packet.c

noinst_HEADERS = net_driver.h

#
# netsource in-process client
#

netsource_clientdir = $(ADDON_DIR)

netsource_client_LTLIBRARIES = netsource.la 

netsource_la_LDFLAGS = -module -avoid-version @OS_LDFLAGS@
netsource_la_SOURCES = net_source.c netjack_packet.c 

netsource_la_LIBADD = -lsamplerate

endif
