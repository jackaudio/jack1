MAINTAINERCLEANFILES = Makefile.in jackd.1 jack_md5.h 

if USE_CAPABILITIES
CAP_LIBS = -lcap
install-exec-hook:
	if [ "`id -u`" = "0" ]; \
	then	chown root.root $(DESTDIR)$(bindir)/jackd; \
			chmod 555 $(DESTDIR)$(bindir)/jackd; \
			chown root.root $(DESTDIR)$(bindir)/jackstart; \
			chmod 4555 $(DESTDIR)$(bindir)/jackstart; \
	else    echo "You must be root to set ownership and permissions"; \
			echo " for jackd and jackstart."; \
	fi
else
CAP_LIBS =
install-exec-hook:
	@echo "Nothing to make for $@."
endif

if USE_CAPABILITIES
bin_PROGRAMS = jackd jackstart
else
bin_PROGRAMS = jackd
endif

AM_CFLAGS = $(JACK_CFLAGS) -DJACKD_LOCATION=\"$(bindir)/jackd\"

jackd_SOURCES = jackd.c engine.c transengine.c
jackd_LDADD = ../libjack/libjack.la -lm -ldl -lrt -lpthread $(CAP_LIBS)

jackstart_SOURCES = jack_md5.h jackstart.c md5.c
jackstart_LDFLAGS = $(CAP_LIBS)

noinst_HEADERS = md5.h md5_loc.h transengine.h

jack_md5.h: jackd
if STRIPPED_JACKD
	strip -R .note -R .comment .libs/jackd
endif
	echo "#define JACKD_MD5_SUM \"`md5sum .libs/jackd | awk '{print $$1}'`\"" > jack_md5.h

man_MANS = jackd.1 jackstart.1
EXTRA_DIST = $(man_MANS)

dist-hook:
	rm -f $(distdir)/jack_md5.h
distclean-local:
	rm -f $(top_builddir)/jackd/jack_md5.h
