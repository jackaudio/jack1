# Currently, we don't actually build anything in the `cpu' and `os'
# subdirectories.  If we ever do, they will need Makefiles.  For now,
# this is sufficient.  Unfortunately, their CVS subdirectories also
# get distributed as a side-effect.

EXTRA_DIST = configure.host depcomp cpu os
MAINTAINERCLEANFILES = Makefile.in aclocal.m4 config.guess config.sub \
                       install-sh ltmain.sh missing mkinstalldirs 

# Link all relevant headers into the `sysdeps' directory.  The order
# of these loops determines header file precedence.

SUBDIRS =	sysdeps
DIST_SUBDIRS =	sysdeps
