MAINTAINERCLEANFILES    = Makefile.in

AM_CFLAGS = $(JACK_CFLAGS) $(GLIB_CFLAGS)
AM_CXXFLAGS = $(JACK_CFLAGS)

lib_LTLIBRARIES    = libjack.la

libjack_la_SOURCES = client.c pool.c driver.c
libjack_la_LIBADD  = $(GLIB_LIBS) -lm -lpthread
libjack_la_LDFLAGS  = -export-dynamic -version-info @JACK_SO_VERSION@

