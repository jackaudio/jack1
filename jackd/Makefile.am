MAINTAINERCLEANFILES = Makefile.in

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

jackdincludedir = $(includedir)/jack

jackdinclude_HEADERS = md5.h

BUILT_SOURCES = md5.h

md5.h: jackd
	echo "#define JACKD_MD5_SUM \"`md5sum .libs/jackd | awk '{print $$1}'`\"" > md5.h

jackstart_SOURCES = jackstart.c
jackstart_LDFLAGS = -lcap -lssl

dist-hook:
	rm -f $(distdir)/md5.h
distclean-local:
	rm -f $(top_builddir)/jackd/md5.h
