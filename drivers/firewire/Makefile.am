# plugin is called 'firewire',
# driver code is called 'ffado*' because it is an implementation
# of a firewire backend

MAINTAINERCLEANFILES=Makefile.in

AM_CFLAGS = $(JACK_CFLAGS) $(LIBFFADO_CFLAGS) $(ALSA_CFLAGS)

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_firewire.la

jack_firewire_la_SOURCES = ffado_driver.c

jack_firewire_la_LIBADD = $(LIBFFADO_LIBS) $(ALSA_LIBS) $(top_builddir)/libjack/libjack.la $(top_builddir)/jackd/libjackserver.la

jack_firewire_la_LDFLAGS = -module -avoid-version

noinst_HEADERS = ffado_driver.h

