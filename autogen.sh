#!/bin/sh

libtoolize --force && aclocal $ACLOCAL_FLAGS && automake --add-missing --foreign && autoconf

exit 0

