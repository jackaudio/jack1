# This directory contains links to platform-dependent headers in the
# source tree.  None are installed or distributed from here.  Remember
# that the build tree and the source tree are not always the same.

MAINTAINERCLEANFILES = Makefile.in
CLEANFILES = *.h

# Link all relevant headers into the `sysdeps' directory.  The order
# of these loops determines header file precedence.

all-local:
	for h in ${top_srcdir}/config/cpu/generic/*.h; do \
	    $(LN_S) -f $$h .; \
	done
	for h in ${top_srcdir}/config/@cpu_include_dir@/*.h; do \
	    $(LN_S) -f $$h .; \
	done
	for h in ${top_srcdir}/config/os/generic/*.h; do \
	    $(LN_S) -f $$h .; \
	done
	for h in ${top_srcdir}/config/@os_include_dir@/*.h; do \
	    $(LN_S) -f $$h .; \
	done
