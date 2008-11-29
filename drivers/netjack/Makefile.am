MAINTAINCLEANFILES = Makefile.in

AM_CFLAGS = $(JACK_CFLAGS) 

if HAVE_SAMPLERATE

if HAVE_CELT
AM_CFLAGS += -DHAVE_CELT
endif
if HAVE_PPOLL
AM_CFLAGS += -DHAVE_PPOLL
endif

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_net.la

jack_net_la_LIBADD = -lsamplerate

jack_net_la_LDFLAGS = -module -avoid-version 
jack_net_la_SOURCES = net_driver.c netjack_packet.c

noinst_HEADERS = net_driver.h netjack_packet.h

endif # HAVE_SAMPLERATE
