## Process this file with automake to produce Makefile.in
MAINTAINERCLEANFILES=Makefile.in 

CLEANFILES=doxygen-build.stamp

DOX=reference.doxygen
DOXSOURCES=mainpage.dox transport.dox porting.dox fsm.png fsm.eps \
	../example-clients/inprocess.c \
	../example-clients/simple_client.c \
	../jack/intclient.h \
	../jack/jack.h \
	../jack/port.h \
	../jack/ringbuffer.h \
	../jack/thread.h \
	../jack/transport.h \
	../jack/types.h \
	../jack/midiport.h \
	../jack/session.h \
	../jack/weakjack.h

EXTRA_DIST=mainpage.dox transport.dox fsm.png fsm.eps porting.dox

INSTIMAGES=reference/html/doxygen.png reference/html/fsm.png

DOC_STAMPS=html-build.stamp

DOC_DIR=$(HTML_DIR)

all-local: doxygen-build.stamp

doxygen-build.stamp: $(DOX) $(DOXSOURCES)
	@echo '*** Running doxygen ***'
	doxygen $(DOX)
	touch doxygen-build.stamp

clean-local:
	rm -f *~ *.bak $(DOC_STAMPS) || true
	if test -d html; then rm -fr html; fi
	if test -d latex; then rm -fr latex; fi

distclean-local: clean
	rm -f *.stamp || true
	if test -d reference; then rm -rf reference; fi

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(DOC_DIR)/reference/html
	(installfiles=`echo reference/html/*.html`; \
	if test "$$installfiles" = 'reference/html/*.html'; \
	then echo '-- Nothing to install' ; \
	else \
	  for i in $$installfiles $(INSTIMAGES) reference/html/doxygen.css; do \
	    echo '-- Installing '$$i ; \
	    $(INSTALL_DATA) $$i $(DESTDIR)$(DOC_DIR)/reference/html; \
	  done; \
	fi)

# we need doxygen stuff built so we can know what to uninstall
uninstall-local: doxygen-build.stamp
	(installfiles=`echo reference/html/*.html`; \
	if test "$$installfiles" = 'reference/html/*.html'; \
	then echo '-- Nothing to uninstall' ; \
	else \
	  for i in $$installfiles $(INSTIMAGES) reference/html/doxygen.css; do \
	    echo '-- Unstalling '$$i ; \
	    rm $(DESTDIR)$(DOC_DIR)/$$i; \
	  done; \
	fi)

dist-hook: dist-hook-local
	mkdir $(distdir)/reference
	mkdir $(distdir)/reference/html
	-cp reference/html/*.html reference/html/*.css \
	    reference/html/*.png  $(distdir)/reference/html

.PHONY : dist-hook-local
