MAINTAINERCLEANFILES = Makefile.in

if HAVE_ALSA
ALSA_DIR = alsa
else
ALSA_DIR =
endif

if HAVE_PA
PA_DIR = portaudio
else
PA_DIR =
endif

SUBDIRS = dummy iec61883 $(ALSA_DIR) $(PA_DIR)

DIST_SUBDIRS = dummy iec61883 alsa portaudio
