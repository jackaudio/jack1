MAINTAINERCLEANFILES    = Makefile.in

SOURCE_FILES = client.c pool.c driver.c timestamps.c

lib_LTLIBRARIES	   = libjack.la

AM_CFLAGS = $(JACK_CFLAGS)
AM_CXXFLAGS = $(JACK_CFLAGS)

libjack_la_CFLAGS = $(AM_CFLAGS)

libjack_la_SOURCES = $(SOURCE_FILES)
libjack_la_LIBADD  = -lm -lpthread
libjack_la_LDFLAGS  = -export-dynamic -version-info @JACK_SO_VERSION@
