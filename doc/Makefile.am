## Process this file with automake to produce Makefile.in
MAINTAINERCLEANFILES=Makefile.in 

CLEANFILES=doxygen-build.stamp

DOX=reference.doxygen

EXTRA_DIST=mainpage.dox transport.dox fsm.png

INSTIMAGES=html/doxygen.png html/fsm.png

DOC_STAMPS=html-build.stamp

DOC_DIR=$(HTML_DIR)

all-local: doxygen-build.stamp

doxygen-build.stamp: $(DOX) 
	@echo '*** Running doxygen ***'
	doxygen $(DOX)
	touch doxygen-build.stamp

clean-local:
	rm -f *~ *.bak $(DOC_STAMPS) || true
	if test -d html; then rm -fr html; fi
	if test -d latex; then rm -fr latex; fi

distclean-local: clean
	rm -f *.stamp || true
	if test -d html; then rm -rf html; fi
	if test -d latex; then rm -rf latex; fi

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(DOC_DIR)/html
	(installfiles=`echo html/*.html`; \
	if test "$$installfiles" = 'html/*.html'; \
	then echo '-- Nothing to install' ; \
	else \
	  for i in $$installfiles $(INSTIMAGES) html/doxygen.css; do \
	    echo '-- Installing '$$i ; \
	    $(INSTALL_DATA) $$i $(DESTDIR)$(DOC_DIR)/html; \
	  done; \
	fi)

# we need doxygen stuff built so we can know what to uninstall
uninstall-local: doxygen-build.stamp
	(installfiles=`echo html/*.html`; \
	if test "$$installfiles" = 'html/*.html'; \
	then echo '-- Nothing to uninstall' ; \
	else \
	  for i in $$installfiles $(INSTIMAGES) html/doxygen.css; do \
	    echo '-- Unstalling '$$i ; \
	    rm $(DESTDIR)$(DOC_DIR)/$$i; \
	  done; \
	fi)

dist-hook: dist-hook-local
	mkdir $(distdir)/html
	-cp html/*.html html/*.css \
	    html/*.png  $(distdir)/html

.PHONY : dist-hook-local
