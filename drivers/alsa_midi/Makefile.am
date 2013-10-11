MAINTAINERCLEANFILES    = Makefile.in

#
# slave driver to bridge to ALSA (sequencer) MIDI
#

driverdir = $(ADDON_DIR)

driver_LTLIBRARIES = jack_alsa_midi.la

jack_alsa_midi_la_LDFLAGS = -module -avoid-version @OS_LDFLAGS@
jack_alsa_midi_la_SOURCES = alsa_midi.c port.c port_hash.c port_thread.c list.c
jack_alsa_midi_la_LIBADD = $(ALSA_LIBS) 

noinst_HEADERS = a2j.h list.h port.h port_hash.h port_thread.h midi_pack.h midi_unpack.h


