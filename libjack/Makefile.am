MAINTAINERCLEANFILES    = Makefile.in aclocal.m4 configure config.h.in \
                          stamp-h.in \
			  config.guess mkinstalldirs \
                          missing install-sh config.sub ltconfig \
                          ltmain.sh acinclude.m4

AM_CFLAGS = -I.. $(JACK_CFLAGS) -DADDON_DIR=\"$(ADDON_DIR)\" $(GLIB_CFLAGS)
AM_CXXFLAGS = -I.. $(JACK_CFLAGS) -DADDON_DIR=\"$(ADDON_DIR)\"

lib_LTLIBRARIES    = libjack.la

libjack_la_SOURCES = client.c pool.c driver.c
libjack_la_LDFLAGS = $(GLIB_LIBS) -lm

