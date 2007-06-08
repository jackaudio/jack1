MAINTAINERCLEANFILES=Makefile.in

AM_CFLAGS = $(JACK_CFLAGS) $(LIBFREEBOB_CFLAGS) $(ALSA_CFLAGS)

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_freebob.la

jack_freebob_la_SOURCES = freebob_driver.c

jack_freebob_la_LIBADD = $(LIBFREEBOB_LIBS) $(ALSA_LIBS) 

if HAVE_ALSA_MIDI
jack_freebob_la_SOURCES += ../alsa-midi/alsa_rawmidi.c ../alsa-midi/alsa_seqmidi.c
jack_freebob_la_CFLAGS = -I../alsa-midi
endif

jack_freebob_la_LDFLAGS = -module -avoid-version

noinst_HEADERS = freebob_driver.h

