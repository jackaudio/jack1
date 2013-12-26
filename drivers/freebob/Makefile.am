MAINTAINERCLEANFILES=Makefile.in

AM_CFLAGS = $(JACK_CFLAGS) $(LIBFREEBOB_CFLAGS) $(ALSA_CFLAGS)

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_freebob.la

jack_freebob_la_SOURCES = freebob_driver.c

jack_freebob_la_LIBADD = $(LIBFREEBOB_LIBS) $(ALSA_LIBS) 

jack_freebob_la_LDFLAGS = -module -avoid-version

noinst_HEADERS = freebob_driver.h

