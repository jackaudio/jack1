MAINTAINERCLEANFILES=Makefile.in

AM_CFLAGS = $(JACK_CFLAGS)

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_alsa.la

jack_alsa_la_LDFLAGS = -module -avoid-version
jack_alsa_la_SOURCES = alsa_driver.c generic_hw.c memops.c \
		       hammerfall.c hdsp.c ice1712.c

noinst_HEADERS = alsa_driver.h \
		generic.h \
		hammerfall.h \
		hdsp.h \
		ice1712.h \
		memops.h

jack_alsa_la_LIBADD = $(ALSA_LIBS)
