MAINTAINERCLEANFILES=Makefile.in

AM_CFLAGS = $(JACK_CFLAGS)

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_dummy.la

jack_dummy_la_LDFLAGS = -module -avoid-version
jack_dummy_la_SOURCES = dummy_driver.c dummy_driver.h

# jack_alsa_la_LIBADD = 
