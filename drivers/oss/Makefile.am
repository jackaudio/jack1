MAINTAINCLEANFILES = Makefile.in

AM_CFLAGS = $(JACK_CFLAGS) -I/opt/oss/include -D_XOPEN_SOURCE=600

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_oss.la

jack_oss_la_LDFLAGS = -module -avoid-version -lpthread
jack_oss_la_SOURCES = oss_driver.c oss_driver.h

noinst_HEADERS = oss_driver.h
