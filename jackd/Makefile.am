MAINTAINERCLEANFILES = Makefile.in

if USE_CAPABILITIES
bin_PROGRAMS = jackd jackstart
else
bin_PROGRAMS = jackd
endif

AM_CFLAGS = $(JACK_CFLAGS) -DADDON_DIR=\"$(ADDON_DIR)\" $(GLIB_CFLAGS)

jackd_SOURCES = jackd.c engine.c
jackd_LDADD = ../libjack/libjack.la

if USE_CAPABILITIES
jackd_LDFLAGS = -lm -ldl -lpthread $(GLIB_LIBS) -lcap
else
jackd_LDFLAGS = -lm -ldl -lpthread $(GLIB_LIBS)
endif

jackdincludedir = $(includedir)/jack

jackdinclude_HEADERS = md5.h

BUILT_SOURCES = md5.h

md5.h: jackd
	echo "#define JACKD_MD5_SUM \"`md5sum jackd | awk '{print $1}'`\"" > md5.h

dist-hook:
	rm -f $(distdir)/md5.h
distclean-local:
	rm -f $(top_builddir)/jackd/md5.h

jackstart_SOURCES = jackstart.c
jackstart_LDFLAGS = -lcap -lssl


