MAINTAINERCLEANFILES = Makefile.in jack_md5.h

if USE_CAPABILITIES
CAP_PROGS = jackstart
CAP_LIBS = -lcap
else
CAP_PROGS =
CAP_LIBS =
endif

bin_PROGRAMS = jackd $(CAP_PROGS)

AM_CFLAGS = $(JACK_CFLAGS) -DJACKD_LOCATION=\"$(bindir)/jackd\" $(GLIB_CFLAGS)

jackd_SOURCES = jackd.c engine.c
jackd_LDADD = ../libjack/libjack.la -lm -ldl -lpthread $(GLIB_LIBS) $(CAP_LIBS)

noinst_HEADERS = jack_md5.h md5.h md5_loc.h

BUILT_SOURCES = jack_md5.h

jack_md5.h: jackd
if STRIPPED_JACKD
	strip -R .note -R .comment .libs/jackd
endif
	echo "#define JACKD_MD5_SUM \"`md5sum .libs/jackd | awk '{print $$1}'`\"" > jack_md5.h

jackstart_SOURCES = jackstart.c md5.c
jackstart_LDFLAGS = -lcap

dist-hook:
	rm -f $(distdir)/jack_md5.h
distclean-local:
	rm -f $(top_builddir)/jackd/jack_md5.h
