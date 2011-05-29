MAINTAINERCLEANFILES = Makefile.in jackd.1 jack_md5.h 

if USE_CAPABILITIES
CAP_PROGS = jackstart
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
CAP_PROGS =
CAP_LIBS =
install-exec-hook:
	@echo "Nothing to make for $@."
endif

bin_PROGRAMS = jackd $(CAP_PROGS)

AM_CFLAGS = $(JACK_CFLAGS) -DJACK_LOCATION=\"$(bindir)\"

jackd_SOURCES = jackd.c
jackd_LDADD = libjackserver.la $(CAP_LIBS) @OS_LDFLAGS@

noinst_HEADERS = jack_md5.h md5.h md5_loc.h \
		 clientengine.h transengine.h

BUILT_SOURCES = jack_md5.h

jack_md5.h: jackd
if STRIPPED_JACKD
	strip -R .note -R .comment .libs/jackd
endif
	echo "#define JACKD_MD5_SUM \"`md5sum .libs/jackd | awk '{print $$1}'`\"" > jack_md5.h

jackstart_SOURCES = jackstart.c md5.c
jackstart_LDFLAGS = -lcap

lib_LTLIBRARIES	= libjackserver.la

libjackserver_la_CFLAGS = $(AM_CFLAGS)

libjackserver_la_SOURCES = engine.c clientengine.c transengine.c controlapi.c \
	../libjack/systemtest.c ../libjack/sanitycheck.c \
	../libjack/client.c ../libjack/driver.c ../libjack/intclient.c \
        ../libjack/messagebuffer.c ../libjack/pool.c ../libjack/port.c \
        ../libjack/midiport.c ../libjack/ringbuffer.c ../libjack/shm.c \
        ../libjack/thread.c ../libjack/time.c  ../libjack/transclient.c \
        ../libjack/unlock.c
libjackserver_la_LIBADD  = simd.lo @OS_LDFLAGS@
libjackserver_la_LDFLAGS  = -export-dynamic -version-info @JACK_SO_VERSION@

simd.lo: $(srcdir)/../libjack/simd.c
	$(LIBTOOL) --mode=compile $(CC) -I$(top_builddir) $(JACK_CORE_CFLAGS) $(SIMD_CFLAGS) -c -o simd.lo $(srcdir)/../libjack/simd.c

man_MANS = jackd.1 jackstart.1
EXTRA_DIST = $(man_MANS)

dist-hook:
	rm -f $(distdir)/jack_md5.h
distclean-local:
	rm -f $(top_builddir)/jackd/jack_md5.h
