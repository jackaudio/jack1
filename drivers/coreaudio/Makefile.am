MAINTAINERCLEANFILES=Makefile.in

AM_CFLAGS = $(JACK_CFLAGS) -include /System/Library/Frameworks/Carbon.framework/Headers/Carbon.h

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_coreaudio.la

jack_coreaudio_la_LDFLAGS = -module -avoid-version @OS_LDFLAGS@
jack_coreaudio_la_SOURCES = coreaudio_driver.c coreaudio_driver.h 

noinst_HEADERS = coreaudio_driver.h 
