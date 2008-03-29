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

#
# alsa-in and alsa-out tools
#
if HAVE_ALSA
bin_PROGRAMS = alsa_in \
               alsa_out

alsa_in_SOURCES = alsa_in.c
alsa_in_LDFLAGS = -lasound -lsamplerate @OS_LDFLAGS@
alsa_in_LDADD = ../../libjack/libjack.la

alsa_out_SOURCES = alsa_out.c
alsa_out_LDFLAGS = -lasound -lsamplerate @OS_LDFLAGS@
alsa_out_LDADD = ../../libjack/libjack.la
endif # HAVE_ALSA

endif # HAVE_SAMPLERATE
