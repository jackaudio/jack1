MAINTAINERCLEANFILES    = Makefile.in

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

if HAVE_READLINE
JACK_TRANSPORT = jack_transport
dist-check-readline:
else
JACK_TRANSPORT =
dist-check-readline:
	@echo
	@echo ' ******' You need readline installed to make dist.' ******'
	@echo
	@false
endif

bin_PROGRAMS = jack_load \
	       jack_unload \
	       jack_simple_client \
	       jack_monitor_client \
	       jack_impulse_grabber \
	       jack_connect \
	       jack_disconnect \
	       jack_metro \
	       jack_showtime \
               jack_bufsize \
	       jack_lsp \
	       jack_freewheel \
	       $(JACKREC) \
	       $(JACK_TRANSPORT)

if HAVE_SNDFILE
# note! jackrec_CFLAGS syntax not supported by automake-1.4
sndfile_cflags = @SNDFILE_CFLAGS@
endif

AM_CFLAGS = -I.. $(JACK_CFLAGS) $(sndfile_cflags)
AM_CXXFLAGS = -I.. $(JACK_CFLAGS) $(sndfile_cflags)

jack_simple_client_SOURCES = simple_client.c
jack_simple_client_LDFLAGS = @OS_LDFLAGS@
jack_simple_client_LDADD = ../libjack/libjack.la

jack_connect_SOURCES = connect.c
jack_connect_LDFLAGS = @OS_LDFLAGS@
jack_connect_LDADD = ../libjack/libjack.la

jack_disconnect_SOURCES = connect.c
jack_disconnect_LDFLAGS = @OS_LDFLAGS@
jack_disconnect_LDADD = ../libjack/libjack.la

jack_monitor_client_SOURCES = monitor_client.c
jack_monitor_client_LDFLAGS = @OS_LDFLAGS@
jack_monitor_client_LDADD = ../libjack/libjack.la

jack_metro_SOURCES = metro.c
jack_metro_LDFLAGS = @OS_LDFLAGS@
jack_metro_LDADD = ../libjack/libjack.la

jack_lsp_SOURCES = lsp.c
jack_lsp_LDFLAGS = @OS_LDFLAGS@
jack_lsp_LDADD = ../libjack/libjack.la

jack_showtime_SOURCES = showtime.c
jack_showtime_LDFLAGS = @OS_LDFLAGS@
jack_showtime_LDADD = ../libjack/libjack.la

jack_bufsize_SOURCES = bufsize.c
jack_bufsize_LDFLAGS = @OS_LDFLAGS@
jack_bufsize_LDADD = ../libjack/libjack.la

jack_freewheel_SOURCES = freewheel.c
jack_freewheel_LDFLAGS = @OS_LDFLAGS@
jack_freewheel_LDADD = ../libjack/libjack.la

if HAVE_SNDFILE
jackrec_SOURCES = capture_client.c
jackrec_LDFLAGS = @SNDFILE_LIBS@ @OS_LDFLAGS@
jackrec_LDADD = ../libjack/libjack.la
endif

if HAVE_READLINE
jack_transport_SOURCES = transport.c
jack_transport_LDFLAGS = -lreadline @READLINE_DEPS@ @OS_LDFLAGS@
jack_transport_LDADD = ../libjack/libjack.la
endif

jack_impulse_grabber_SOURCES = impulse_grabber.c
jack_impulse_grabber_LDFLAGS = @OS_LDFLAGS@
jack_impulse_grabber_LDADD = ../libjack/libjack.la

#
# general purpose in-process loader
#

jack_load_SOURCES = ipload.c
jack_load_LDFLAGS = @OS_LDFLAGS@
jack_load_LDADD = ../libjack/libjack.la

jack_unload_SOURCES = ipunload.c
jack_unload_LDFLAGS = @OS_LDFLAGS@
jack_unload_LDADD = ../libjack/libjack.la

#
# sample in-process client(s)
#

ip_clientdir = $(ADDON_DIR)

ip_client_LTLIBRARIES = inprocess.la intime.la

inprocess_la_LDFLAGS = -module -avoid-version @OS_LDFLAGS@
inprocess_la_SOURCES = inprocess.c

intime_la_LDFLAGS = -module -avoid-version @OS_LDFLAGS@
intime_la_SOURCES = intime.c

dist-hook: dist-check-sndfile
