MAINTAINERCLEANFILES = Makefile.in

if HAVE_ALSA
ALSA_DIR = alsa
else
ALSA_DIR =
endif

if HAVE_RAW1394
IEC61883_DIR = iec61883
else
IEC61883_DIR =
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

SUBDIRS = $(ALSA_DIR) dummy $(IEC61883_DIR) $(OSS_DIR) $(PA_DIR) $(CA_DIR)
DIST_SUBDIRS = alsa dummy iec61883 oss portaudio coreaudio
