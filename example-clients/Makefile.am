MAINTAINERCLEANFILES    = Makefile.in

if HAVE_FLTK
FLTK_CLIENT = jack_fltk_client
dist-check-fltk:
else
FLTK_CLIENT =
dist-check-fltk:
	@echo
	@echo ' ******' You need fltk installed to make dist.' ******'
	@echo
	@false
endif

if HAVE_SNDFILE
JACKREC = jackrec
dist-check-sndfile:
else
JACKREC =
dist-check-sndfile:
	@echo
	@echo ' ******' You need sndfile installed to make dist.' ******'
	@echo
	@false
endif

bin_PROGRAMS = jack_simple_client \
	       jack_monitor_client \
	       jack_impulse_grabber \
	       jack_cache_killer \
	       jack_connect \
	       jack_disconnect \
	       $(FLTK_CLIENT) $(JACKREC)

AM_CFLAGS = -I.. $(JACK_CFLAGS) $(GLIB_CFLAGS)
AM_CXXFLAGS = -I.. $(JACK_CFLAGS)

jack_simple_client_SOURCES = simple_client.c
jack_simple_client_LDFLAGS = -ldl -lpthread
jack_simple_client_LDADD = ../libjack/libjack.la

jack_connect_SOURCES = connect.c
jack_connect_LDFLAGS = -ldl -lpthread
jack_connect_LDADD = ../libjack/libjack.la

jack_disconnect_SOURCES = connect.c
jack_disconnect_LDFLAGS = -ldl -lpthread
jack_disconnect_LDADD = ../libjack/libjack.la

jack_monitor_client_SOURCES = monitor_client.c
jack_monitor_client_LDFLAGS = -ldl -lpthread
jack_monitor_client_LDADD = ../libjack/libjack.la

jack_cache_killer_SOURCES = cache_killer.c
jack_cache_killer_LDFLAGS = -ldl -lpthread
jack_cache_killer_LDADD = ../libjack/libjack.la

if HAVE_FLTK
jack_fltk_client_SOURCES = fltk_client.cc
jack_fltk_client_LDFLAGS = -L/usr/X11R6/lib -lfltk -lX11 -lXext -ldl -lpthread
jack_fltk_client_LDADD = ../libjack/libjack.la
endif

if HAVE_SNDFILE
jackrec_SOURCES = capture_client.c
jackrec_LDFLAGS = -lsndfile -ldl -lpthread
jackrec_LDADD = ../libjack/libjack.la
endif

jack_impulse_grabber_SOURCES = impulse_grabber.c
jack_impulse_grabber_LDFLAGS = -ldl -lpthread -lm
jack_impulse_grabber_LDADD = ../libjack/libjack.la

dist-hook: dist-check-fltk dist-check-sndfile
