MAINTAINERCLEANFILES    = Makefile.in

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

NETJACK_TOOLS = jack_netsource

if HAVE_SAMPLERATE
if HAVE_ALSA
NETJACK_TOOLS += alsa_in alsa_out
endif
dist-check-samplerate:
else
dist-check-samplerate:
	@echo
	@echo ' ******' You need libsamplerate installed to make dist.' ******'
	@echo
	@false
endif

bin_PROGRAMS = jack_load \
	       jack_unload \
	       jack_monitor_client \
	       jack_connect \
	       jack_disconnect \
	       jack_lsp \
	       jack_freewheel \
	       jack_evmon \
	       jack_alias \
	       jack_bufsize \
	       jack_samplerate \
	       jack_session_notify \
	       jack_wait \
	       jack_midi_dump \
	       $(JACK_TRANSPORT) \
	       $(NETJACK_TOOLS)

noinst_PROGRAMS = jack_thread_wait

if HAVE_SNDFILE
# note! jackrec_CFLAGS syntax not supported by automake-1.4
sndfile_cflags = @SNDFILE_CFLAGS@
endif

AM_CFLAGS = -I.. $(JACK_CFLAGS) $(sndfile_cflags)
AM_CXXFLAGS = -I.. $(JACK_CFLAGS) $(sndfile_cflags)

jack_connect_SOURCES = connect.c
jack_connect_LDFLAGS = @OS_LDFLAGS@
jack_connect_LDADD = $(top_builddir)/libjack/libjack.la

jack_disconnect_SOURCES = connect.c
jack_disconnect_LDFLAGS = @OS_LDFLAGS@
jack_disconnect_LDADD = $(top_builddir)/libjack/libjack.la

jack_monitor_client_SOURCES = monitor_client.c
jack_monitor_client_LDFLAGS = @OS_LDFLAGS@
jack_monitor_client_LDADD = $(top_builddir)/libjack/libjack.la

jack_thread_wait_SOURCES = tw.c
jack_thread_wait_LDFLAGS = @OS_LDFLAGS@
jack_thread_wait_LDADD = $(top_builddir)/libjack/libjack.la

jack_wait_SOURCES = wait.c
jack_wait_LDFLAGS = @OS_LDFLAGS@
jack_wait_LDADD = $(top_builddir)/libjack/libjack.la

jack_evmon_SOURCES = evmon.c
jack_evmon_LDFLAGS = @OS_LDFLAGS@
jack_evmon_LDADD = $(top_builddir)/libjack/libjack.la

jack_alias_SOURCES = alias.c
jack_alias_LDFLAGS = @OS_LDFLAGS@
jack_alias_LDADD = $(top_builddir)/libjack/libjack.la

jack_lsp_SOURCES = lsp.c
jack_lsp_LDFLAGS = @OS_LDFLAGS@
jack_lsp_LDADD = $(top_builddir)/libjack/libjack.la

jack_freewheel_SOURCES = freewheel.c
jack_freewheel_LDFLAGS = @OS_LDFLAGS@
jack_freewheel_LDADD = $(top_builddir)/libjack/libjack.la

jack_bufsize_SOURCES = bufsize.c
jack_bufsize_LDFLAGS = @OS_LDFLAGS@
jack_bufsize_LDADD = $(top_builddir)/libjack/libjack.la

jack_samplerate_SOURCES = samplerate.c
jack_samplerate_LDFLAGS = @OS_LDFLAGS@
jack_samplerate_LDADD = $(top_builddir)/libjack/libjack.la

jack_session_notify_SOURCES = session_notify.c
jack_session_notify_LDFLAGS = @OS_LDFLAGS@
jack_session_notify_LDADD = $(top_builddir)/libjack/libjack.la

jack_midi_dump_SOURCES = midi_dump.c
jack_midi_dump_LDFLAGS = @OS_LDFLAGS@
jack_midi_dump_LDADD = $(top_builddir)/libjack/libjack.la

if HAVE_READLINE
jack_transport_SOURCES = transport.c
jack_transport_LDFLAGS = -lreadline @READLINE_DEPS@ @OS_LDFLAGS@
jack_transport_LDADD = $(top_builddir)/libjack/libjack.la
endif

#
# General purpose in-process loader/unloader
#

jack_load_SOURCES = ipload.c
jack_load_LDFLAGS = @OS_LDFLAGS@
jack_load_LDADD = $(top_builddir)/libjack/libjack.la

jack_unload_SOURCES = ipunload.c
jack_unload_LDFLAGS = @OS_LDFLAGS@
jack_unload_LDADD = $(top_builddir)/libjack/libjack.la

#
# Netjack slave tools
#
jack_netsource_SOURCES = netsource.c $(top_builddir)/drivers/netjack/netjack_packet.c
jack_netsource_CFLAGS = @NETJACK_CFLAGS@ -I$(top_srcdir)/drivers/netjack
jack_netsource_LDFLAGS = @NETJACK_LIBS@ @OS_LDFLAGS@ 
jack_netsource_LDADD = $(top_builddir)/libjack/libjack.la

if HAVE_SAMPLERATE
if HAVE_ALSA
alsa_in_SOURCES = alsa_in.c $(top_builddir)/drivers/alsa/memops.c
alsa_in_CFLAGS = @NETJACK_CFLAGS@ -I$(top_builddir)/drivers/alsa
alsa_in_LDFLAGS = -lasound -lsamplerate @OS_LDFLAGS@
alsa_in_LDADD = $(top_builddir)/libjack/libjack.la

alsa_out_SOURCES = alsa_out.c $(top_builddir)/drivers/alsa/memops.c
alsa_out_CFLAGS = @NETJACK_CFLAGS@ -I$(top_builddir)/drivers/alsa
alsa_out_LDFLAGS = -lasound -lsamplerate @OS_LDFLAGS@
alsa_out_LDADD = $(top_builddir)/libjack/libjack.la
endif #HAVE_ALSA
endif #HAVE_SAMPLERATE

# XXX ? dist-hook: dist-check-sndfile dist-check-samplerate
