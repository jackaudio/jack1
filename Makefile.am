MAINTAINERCLEANFILES    = Makefile.in aclocal.m4 configure config.h.in \
                          stamp-h.in \
			  config.guess mkinstalldirs \
                          missing install-sh config.sub ltconfig \
                          ltmain.sh acinclude.m4

if HAVE_DOXYGEN
SUBDIRS = jack libjack jackd drivers example-clients doc
dist-check-doxygen:
else
SUBDIRS = jack libjack jackd drivers example-clients
dist-check-doxygen:
	@echo
	@echo ' ******' You need doxygen installed to make dist.' ******'
	@echo
	@false
endif

DIST_SUBDIRS = jack jackd drivers libjack example-clients doc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = jack.pc

rpm: dist
	rpm -ta $(distdir).tar.gz

dist-hook: dist-check-doxygen
