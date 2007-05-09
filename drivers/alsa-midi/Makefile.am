MAINTAINERCLEANFILES=Makefile.in

AM_CFLAGS = $(JACK_CFLAGS)

noinst_LTLIBRARIES        = libalsamidi.la

libalsamidi_la_LDFLAGS = -avoid-version -static
libalsamidi_la_SOURCES = alsa_seqmidi.c alsa_rawmidi.c 

noinst_HEADERS = alsa_midi.h midi_pack.h midi_unpack.h

libalsamidi_la_LIBADD = $(ALSA_LIBS)
