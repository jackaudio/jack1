MAINTAINERCLEANFILES = Makefile.in

if HAVE_ALSA
ALSA_DIR = alsa
else
ALSA_DIR =
endif

if HAVE_ALSA_MIDI
ALSA_MIDI_DIR = alsa-midi
A2J_DIR = a2j
else
ALSA_MIDI_DIR =
endif

if HAVE_OSS
OSS_DIR = oss
else
OSS_DIR =
endif

if HAVE_SUN
SUN_DIR = sun
else
SUN_DIR =
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

if HAVE_FREEBOB
FREEBOB_DIR = freebob
else
FREEBOB_DIR =
endif

if HAVE_FIREWIRE
FIREWIRE_DIR = firewire
else
FIREWIRE_DIR =
endif

SUBDIRS = $(ALSA_MIDI_DIR) $(A2J_DIR) $(ALSA_DIR) dummy $(OSS_DIR) $(SUN_DIR) $(PA_DIR) $(CA_DIR) $(FREEBOB_DIR) $(FIREWIRE_DIR) netjack
DIST_SUBDIRS = alsa alsa-midi a2j dummy oss sun portaudio coreaudio freebob firewire netjack
