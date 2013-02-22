MAINTAINERCLEANFILES = Makefile.in aclocal.m4 configure config.h.in \
                       stamp-h.in config.log config.cache config.status

if HAVE_DOXYGEN
DOC_DIR = doc
dist-check-doxygen:
else
DOC_DIR =
dist-check-doxygen:
	@echo
	@echo ' ******' You need doxygen installed to make dist.' ******'
	@echo
	@false
endif

SUBDIRS =      libjack jackd drivers example-clients tools config $(DOC_DIR) man python
DIST_SUBDIRS = config libjack jackd include drivers example-clients tools doc man python

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = jack.pc

EXTRA_DIST = COPYING COPYING.GPL COPYING.LGPL libjack/simd.c jack.spec

AUTOMAKE_OPTIONS = foreign

rpm: dist
	rpm -ta $(distdir).tar.gz

dist-hook: dist-check-doxygen

libjackincludedir = $(includedir)/jack

libjackinclude_HEADERS =   \
	jack/intclient.h   \
	jack/jack.h        \
	jack/ringbuffer.h  \
	jack/statistics.h  \
	jack/session.h     \
	jack/thread.h      \
	jack/transport.h   \
	jack/types.h       \
	jack/midiport.h    \
	jack/weakmacros.h  \
	jack/weakjack.h    \
	jack/control.h     \
	jack/jslist.h

