MAINTAINERCLEANFILES=Makefile.in

AM_CFLAGS = $(JACK_CFLAGS)

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_alsa_midi.la

jack_alsa_midi_la_LDFLAGS = -module -avoid-version
jack_alsa_midi_la_SOURCES = alsa_rawmidi.c alsa_seqmidi.c alsa_midi_driver.c

noinst_HEADERS = alsa_midi.h \
		 midi_pack.h \
		 midi_unpack.h

jack_alsa_midi_la_LIBADD = $(ALSA_LIBS) 
