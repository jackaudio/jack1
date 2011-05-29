MAINTAINCLEANFILES = Makefile.in

AM_CFLAGS = $(JACK_CFLAGS) -I/usr/lib/oss/include

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_oss.la

jack_oss_la_LDFLAGS = -module -avoid-version
jack_oss_la_SOURCES = oss_driver.c oss_driver.h
jack_oss_la_LIBADD = $(top_builddir)/libjack/libjack.la $(top_builddir)/jackd/libjackserver.la

noinst_HEADERS = oss_driver.h
