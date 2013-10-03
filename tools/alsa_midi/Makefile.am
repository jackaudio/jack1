MAINTAINERCLEANFILES    = Makefile.in

#
# in-process ALSA/JACK MIDI bridge client
#

alsa_mididir = $(ADDON_DIR)

alsa_midi_LTLIBRARIES = alsa_midi.la
alsa_midi_la_LDFLAGS = -module -avoid-version @OS_LDFLAGS@
alsa_midi_la_SOURCES = alsa_midi.c port.c port_hash.c port_thread.c list.c

noinst_HEADERS = a2j.h list.h port.h port_hash.h port_thread.h
