MAINTAINERCLEANFILES=Makefile.in

AM_CFLAGS = $(JACK_CFLAGS)

plugindir = $(ADDON_DIR)

if IEC61883
plugin_LTLIBRARIES = jack_iec61883.la

jack_iec61883_la_SOURCES = \
	iec61883_common.c \
        iec61883_driver.c \
        iec61883_client.c \
	iec61883_ip_client.c

jack_iec61883_la_LIBADD = $(RAW1394_LIBS)
endif

jack_iec61883_la_LDFLAGS = -module -avoid-version

EXTRA_jack_iec61883_la_SOURCES = \
	iec61883_driver.c \
	iec61883_client.c \
	iec61883_common.c \
	iec61883_ip_client.c

noinst_HEADERS = \
	iec61883_client.h
	iec61883_driver.h \
	iec61883_common.h \
	iec61883_ip_client.h
