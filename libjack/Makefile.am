MAINTAINERCLEANFILES    = Makefile.in

SOURCE_FILES = \
	     client.c \
	     driver.c \
	     intclient.c \
	     pool.c \
	     port.c \
	     ringbuffer.c \
	     shm.c \
	     thread.c \
	     timestamps.c \
	     transclient.c \
	     unlock.c

lib_LTLIBRARIES	= libjack.la

noinst_HEADERS	= local.h

AM_CFLAGS = $(JACK_CFLAGS) -DJACK_LOCATION=\"$(bindir)\"
AM_CXXFLAGS = $(JACK_CFLAGS)

libjack_la_CFLAGS = $(AM_CFLAGS)

libjack_la_SOURCES = $(SOURCE_FILES)
libjack_la_LIBADD  = @OS_LDFLAGS@
libjack_la_LDFLAGS  = -export-dynamic -version-info @JACK_SO_VERSION@

