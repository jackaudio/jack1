MAINTAINERCLEANFILES=Makefile.in

AM_CFLAGS = $(JACK_CFLAGS)

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_alsa_pcm.la

jack_alsa_pcm_la_LDFLAGS = -module -avoid-version
jack_alsa_pcm_la_SOURCES = alsa_driver.c generic_hw.c memops.c \
		       hammerfall.c hdsp.c ice1712.c
jack_alsa_pcm_la_LIBADD = $(ALSA_LIBS)
