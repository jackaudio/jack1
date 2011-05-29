MAINTAINERCLEANFILES=Makefile.in

AM_CFLAGS = $(JACK_CFLAGS)

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_dummy.la

jack_dummy_la_LDFLAGS = -module -avoid-version
jack_dummy_la_SOURCES = dummy_driver.c dummy_driver.h

noinst_HEADERS = dummy_driver.h

jack_dummy_la_LIBADD = $(top_builddir)/libjack/libjack.la  $(top_builddir)/jackd/libjackserver.la
