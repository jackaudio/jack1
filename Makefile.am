MAINTAINERCLEANFILES    = Makefile.in aclocal.m4 configure config.h.in \
                          stamp-h.in \
			  config.guess mkinstalldirs \
                          missing install-sh config.sub ltconfig \
                          ltmain.sh acinclude.m4

SUBDIRS = jack

EXTRA_PROGRAMS = jack_fltk_client jackrec

bin_PROGRAMS = jackd jack_simple_client jack_monitor_client \
	jack_impulse_grabber jack_connect jack_disconnect @XTRA@

AM_CFLAGS = $(JACK_CFLAGS) -DADDON_DIR=\"$(ADDON_DIR)\" @GLIB_CFLAGS@

AM_CXXFLAGS = $(JACK_CFLAGS) -DADDON_DIR=\"$(ADDON_DIR)\"

jackd_SOURCES = jackd.c engine.c
jackd_LDFLAGS = -lm -ldl -lpthread -Xlinker -export-dynamic $(GLIB_LIBS)
jackd_LDADD = libjack.la

jack_simple_client_SOURCES = simple_client.c
jack_simple_client_LDFLAGS = -ldl -lpthread
jack_simple_client_LDADD = libjack.la

jack_connect_SOURCES = connect.c
jack_connect_LDFLAGS = -ldl -lpthread
jack_connect_LDADD = libjack.la

jack_disconnect_SOURCES = connect.c
jack_disconnect_LDFLAGS = -ldl -lpthread
jack_disconnect_LDADD = libjack.la

jack_monitor_client_SOURCES = monitor_client.c
jack_monitor_client_LDFLAGS = -ldl -lpthread
jack_monitor_client_LDADD = libjack.la

jack_fltk_client_SOURCES = fltk_client.cc
jack_fltk_client_LDFLAGS = -L/usr/X11R6/lib -lfltk -lX11 -lXext -ldl -lpthread
jack_fltk_client_LDADD = libjack.la

jackrec_SOURCES = capture_client.c
jackrec_LDFLAGS = -lsndfile -ldl -lpthread
jackrec_LDADD = libjack.la

jack_impulse_grabber_SOURCES = impulse_grabber.c
jack_impulse_grabber_LDFLAGS = -ldl -lpthread -lm
jack_impulse_grabber_LDADD = libjack.la

plugindir = $(ADDON_DIR)

lib_LTLIBRARIES    = libjack.la
plugin_LTLIBRARIES = jack_alsa.la

libjack_la_SOURCES = client.c pool.c driver.c
libjack_la_LDFLAGS = $(GLIB_LIBS) -lm

jack_alsa_la_LDFLAGS = -module
jack_alsa_la_SOURCES = alsa_driver.c hammerfall.c generic_hw.c memops.c 
jack_alsa_la_LIBADD = -lasound $(GLIB_LIBS) -lm

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = jack.pc
