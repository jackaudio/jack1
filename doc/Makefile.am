## Process this file with automake to produce Makefile.in

DOX=reference.doxygen

EXTRA_DIST=

DOC_STAMPS=html-build.stamp

DOC_DIR=$(datadir)/doc/jack

all-local: doxygen-build.stamp

doxygen-build.stamp: $(DOX)
	@echo '*** Running doxygen ***'
	doxygen $(DOX)
	touch doxygen-build.stamp

clean-local:
	rm -f *~ *.bak $(DOC_STAMPS) || true

distclean-local: clean
	rm -f *.stamp || true
	if test -d reference; then rm -rf reference; fi

install-data-local:
	$(mkinstalldirs) $(DOC_DIR)/reference/html
	(installfiles=`echo reference/html/*.html`; \
	if test "$$installfiles" = 'reference/html/*.html'; \
	then echo '-- Nothing to install' ; \
	else \
	  for i in $$installfiles reference/html/doxygen.gif reference/html/doxygen.css; do \
	    echo '-- Installing '$$i ; \
	    $(INSTALL_DATA) $$i $(DOC_DIR)/reference/html; \
	  done; \
	fi)

# we need doxygen stuff built so we can know what to uninstall
uninstall-local: doxygen-build.stamp
	(installfiles=`echo reference/html/*.html`; \
	if test "$$installfiles" = 'reference/html/*.html'; \
	then echo '-- Nothing to uninstall' ; \
	else \
	  for i in $$installfiles reference/html/doxygen.gif reference/html/doxygen.css; do \
	    echo '-- Unstalling '$$i ; \
	    rm $(DOC_DIR)/$$i; \
	  done; \
	fi)

#
# Require doxygen when making dist
#
if HAVE_DOXYGEN
dist-check-doxygen:
else
dist-check-doxygen:
	@echo "*** doxygen must be installed and enabled in order to make dist"
	@false
endif

dist-hook: dist-hook-local dist-check-doxygen
	mkdir $(distdir)/reference
	mkdir $(distdir)/reference/html
	-cp reference/html/*.html reference/html/*.css \
	    reference/html/*.gif  $(distdir)/reference/html

.PHONY : dist-hook-local
