MAINTAINCLEANFILES = Makefile.in

AM_CFLAGS = $(JACK_CFLAGS)

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_sun.la

jack_sun_la_LDFLAGS = -module -avoid-version
jack_sun_la_SOURCES = sun_driver.c sun_driver.h

noinst_HEADERS = sun_driver.h
