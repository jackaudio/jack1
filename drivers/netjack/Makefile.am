MAINTAINCLEANFILES = Makefile.in

AM_CFLAGS = $(JACK_CFLAGS) 


plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_net.la


jack_net_la_LDFLAGS = -module -avoid-version 
jack_net_la_SOURCES = net_driver.c netjack_packet.c

noinst_HEADERS = net_driver.h netjack_packet.h

