MAINTAINERCLEANFILES    = Makefile.in

#
# in-process ALSA/JACK MIDI bridge clients (1 for input, 1 for output) 
#

a2j_clientdir = $(ADDON_DIR)

a2j_client_LTLIBRARIES = a2j_in.la

a2j_common_sources = port.c port_hash.c port_thread.c list.c

a2j_in_la_LDFLAGS = -module -avoid-version @OS_LDFLAGS@
a2j_in_la_SOURCES = input_client.c $(a2j_common_sources)

noinst_HEADERS = a2j.h list.h port.h port_hash.h port_thread.h
