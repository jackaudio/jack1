MAINTAINERCLEANFILES=Makefile.in

AM_CFLAGS = $(JACK_CFLAGS) $(GLIB_CFLAGS)

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_alsa.la

jack_alsa_la_LDFLAGS = -module
jack_alsa_la_SOURCES = alsa_driver.c generic_hw.c memops.c \
		       hammerfall.c ice1712.c
jack_alsa_la_LIBADD = $(ALSA_LIBS) $(GLIB_LIBS)
