AM_CFLAGS = -I$(top_srcdir) $(JACK_CFLAGS) -DADDON_DIR=\"$(ADDON_DIR)\" $(GLIB_CFLAGS)

plugindir = $(ADDON_DIR)

plugin_LTLIBRARIES = jack_alsa.la

jack_alsa_la_LDFLAGS = -module
jack_alsa_la_SOURCES = alsa_driver.c hammerfall.c generic_hw.c memops.c
jack_alsa_la_LIBADD = -lasound $(GLIB_LIBS) -lm
