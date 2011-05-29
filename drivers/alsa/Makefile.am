MAINTAINERCLEANFILES=Makefile.in

AM_CFLAGS = $(JACK_CFLAGS)

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_alsa.la

jack_alsa_la_LDFLAGS = -module -avoid-version
jack_alsa_la_SOURCES = alsa_driver.c generic_hw.c memops.c \
		       hammerfall.c hdsp.c ice1712.c usx2y.c \
                       ../alsa-midi/alsa_rawmidi.c ../alsa-midi/alsa_seqmidi.c

jack_alsa_la_CFLAGS = -I../alsa-midi

noinst_HEADERS = alsa_driver.h \
		generic.h \
		hammerfall.h \
		hdsp.h \
		ice1712.h \
		usx2y.h

jack_alsa_la_LIBADD = $(ALSA_LIBS) $(top_builddir)/libjack/libjack.la $(top_builddir)/jackd/libjackserver.la
