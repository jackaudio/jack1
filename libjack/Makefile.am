MAINTAINERCLEANFILES    = Makefile.in

if USE_POSIX_SHM
install-exec-hook:
	@echo "Nothing to make for $@."
else
install-exec-hook:
	@echo "Removing JACK shared memory registry key " $(JACK_SEMAPHORE_KEY)
	-ipcrm -M $(JACK_SEMAPHORE_KEY)
endif

SOURCE_FILES = \
	     client.c \
	     intclient.c \
	     messagebuffer.c \
	     pool.c \
	     port.c \
             midiport.c \
	     ringbuffer.c \
	     shm.c \
	     thread.c \
             time.c \
	     timestamps.c \
	     transclient.c \
	     unlock.c

simd.lo: $(srcdir)/simd.c
	$(LIBTOOL) --mode=compile $(CC) -I$(top_builddir) $(JACK_CORE_CFLAGS) $(SIMD_CFLAGS) -c -o simd.lo $(srcdir)/simd.c

lib_LTLIBRARIES	= libjack.la

noinst_HEADERS	= local.h

AM_CFLAGS = $(JACK_CFLAGS) -DJACK_LOCATION=\"$(bindir)\"
AM_CXXFLAGS = $(JACK_CFLAGS)

libjack_la_CFLAGS = $(AM_CFLAGS)

libjack_la_SOURCES = $(SOURCE_FILES)
libjack_la_LIBADD  = simd.lo @OS_LDFLAGS@
libjack_la_LDFLAGS  = -export-dynamic -version-info @JACK_SO_VERSION@

