MAINTAINERCLEANFILES    = Makefile.in

if HAVE_ALSA
ALSA_DIR = alsa
else
ALSA_DIR =
endif

if HAVE_SOLARIS
SOLARIS_DIR = solaris
else
SOLARIS_DIR =
endif

SUBDIRS = dummy $(ALSA_DIR) $(SOLARIS_DIR)

DIST_SUBDIRS = dummy alsa solaris

