MAINTAINERCLEANFILES    = Makefile.in

SOURCE_FILES = \
	     shm.c \
	     client.c \
	     driver.c \
	     pool.c \
	     port.c \
	     ringbuffer.c \
	     timestamps.c \
	     transclient.c

lib_LTLIBRARIES	= libjack.la

noinst_HEADERS	= local.h

AM_CFLAGS = $(JACK_CFLAGS)
AM_CXXFLAGS = $(JACK_CFLAGS)

libjack_la_CFLAGS = $(AM_CFLAGS)

libjack_la_SOURCES = $(SOURCE_FILES)
libjack_la_LIBADD  = -lm -lpthread -lrt
libjack_la_LDFLAGS  = -export-dynamic -version-info @JACK_SO_VERSION@


