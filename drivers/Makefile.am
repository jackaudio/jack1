MAINTAINERCLEANFILES = Makefile.in

if HAVE_ALSA
ALSA_DIR = alsa
else
ALSA_DIR =
endif

if HAVE_OSS
OSS_DIR = oss
else
OSS_DIR =
endif

if HAVE_PA
PA_DIR = portaudio
else
PA_DIR =
endif

if HAVE_COREAUDIO
CA_DIR = coreaudio
else
CA_DIR =
endif

SUBDIRS = $(ALSA_DIR) dummy $(OSS_DIR) $(PA_DIR) $(CA_DIR)
DIST_SUBDIRS = alsa dummy oss portaudio coreaudio
