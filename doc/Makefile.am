## Process this file with automake to produce Makefile.in
MAINTAINERCLEANFILES=Makefile.in

DOX=reference.doxygen


EXTRA_DIST=

INSTIMAGES=reference/html/doxygen.png reference/html/fsm.png

DOC_STAMPS=html-build.stamp

DOC_DIR=$(HTML_DIR)

all-local: doxygen-build.stamp

doxygen-build.stamp: $(DOX) mainpage.dox transport.dox fsm.png \
../jack/jack.h ../jack/types.h ../jack/transport.h
	@echo '*** Running doxygen ***'
	doxygen $(DOX)
	touch doxygen-build.stamp

clean-local:
	rm -f *~ *.bak $(DOC_STAMPS) || true

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
