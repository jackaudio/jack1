MAINTAINERCLEANFILES    = Makefile.in

if HAVE_GLIB2

LIBJACK_GLIB2 = libjack-glib2.la

endif

SOURCE_FILES = client.c pool.c driver.c

lib_LTLIBRARIES	   = libjack.la $(LIBJACK_GLIB2)

AM_CFLAGS = $(JACK_CFLAGS)
AM_CXXFLAGS = $(JACK_CFLAGS)

libjack_la_CFLAGS = $(GLIB_CFLAGS) $(AM_CFLAGS)
libjack_glib2_la_CFLAGS = $(GLIB2_CFLAGS) $(AM_CFLAGS)

libjack_la_SOURCES = $(SOURCE_FILES)
libjack_la_LIBADD  = $(GLIB_LIBS) -lm -lpthread
libjack_la_LDFLAGS  = -export-dynamic -version-info @JACK_SO_VERSION@

libjack_glib2_la_SOURCES = $(SOURCE_FILES)
libjack_glib2_la_LIBADD  = $(GLIB2_LIBS) -lm -lpthread
libjack_glib2_la_LDFLAGS  = -export-dynamic -version-info @JACK_SO_VERSION@
