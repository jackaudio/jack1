MAINTAINERCLEANFILES    = Makefile.in aclocal.m4 configure config.h.in \
                          stamp-h.in \
			  config.guess mkinstalldirs \
                          missing install-sh config.sub ltconfig \
                          ltmain.sh acinclude.m4

SUBDIRS = jack doc

EXTRA_PROGRAMS = jack_fltk_client

bin_PROGRAMS = jackd jack_simple_client jack_monitor_client @XTRA@

AM_CFLAGS = $(JACK_CFLAGS) -DADDON_DIR=\"$(ADDON_DIR)\" @GLIB_CFLAGS@

AM_CXXFLAGS = $(JACK_CFLAGS) -DADDON_DIR=\"$(ADDON_DIR)\"

jackd_SOURCES = jackd.c engine.c
jackd_LDFLAGS = -L. -ljack -lltdl -lpthread $(GLIB_LIBS)

jack_simple_client_SOURCES = simple_client.c
jack_simple_client_LDFLAGS = -L. -ljack -lltdl -lpthread

jack_monitor_client_SOURCES = monitor_client.c
jack_monitor_client_LDFLAGS = -L. -ljack -lltdl -lpthread

jack_fltk_client_SOURCES = fltk_client.cc
jack_fltk_client_LDFLAGS = -L. -L/usr/X11R6/lib -lfltk -lX11 -lXext -ljack -lltdl -lpthread

lib_LTLIBRARIES    = libjack.la jack_alsa.la

libjack_la_SOURCES = client.c pool.c driver.c
libjack_la_LDFLAGS = $(GLIB_LIBS)

jack_alsa_la_LDFLAGS = -module
jack_alsa_la_SOURCES = alsa_driver.c hammerfall.c generic_hw.c memops.c 
jack_alsa_la_LIBADD = -lasound $(GLIB_LIBS)

EXTRA_DIST = jack

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = jack.pc
