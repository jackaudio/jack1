MAINTAINERCLEANFILES=Makefile.in

AM_CFLAGS = $(JACK_CFLAGS)

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_portaudio.la

jack_portaudio_la_LDFLAGS = -module -avoid-version
jack_portaudio_la_SOURCES = portaudio_driver.c portaudio_driver.h

noinst_HEADERS = portaudio_driver.h

jack_portaudio_la_LIBADD = $(PA_LIBS)
