MAINTAINERCLEANFILES    = Makefile.in aclocal.m4 configure config.h.in \
                          stamp-h.in \
			  config.guess mkinstalldirs \
                          missing install-sh config.sub ltconfig \
                          ltmain.sh acinclude.m4 libjack.la

bin_PROGRAMS = jackd 

AM_CFLAGS = -I.. $(JACK_CFLAGS) -DADDON_DIR=\"$(ADDON_DIR)\" $(GLIB_CFLAGS)
AM_CXXFLAGS = -I.. $(JACK_CFLAGS) -DADDON_DIR=\"$(ADDON_DIR)\"

jackd_SOURCES = jackd.c engine.c
jackd_LDFLAGS = -lm -ldl -lpthread -Xlinker -export-dynamic $(GLIB_LIBS)
jackd_LDADD = ../libjack/libjack.la

