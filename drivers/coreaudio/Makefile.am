MAINTAINERCLEANFILES=Makefile.in

AM_CFLAGS = $(JACK_CFLAGS)

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_coreaudio.la

jack_coreaudio_la_LDFLAGS = -module -avoid-version
jack_coreaudio_la_SOURCES = coreaudio_driver.c coreaudio_driver.h AudioRender.cpp AudioRenderBridge.cpp

noinst_HEADERS = coreaudio_driver.h AudioRender.h AudioRenderBridge.h

jack_coreaudio_la_LIBADD = $(CA_LIBS)
