MAINTAINERCLEANFILES=Makefile.in

AM_CFLAGS = $(JACK_CFLAGS) $(GLIB_CFLAGS)

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_solaris.la

jack_solaris_la_LDFLAGS = -module
jack_solaris_la_SOURCES = solaris_driver.c
jack_solaris_la_LIBADD = $(GLIB_LIBS) -lm
