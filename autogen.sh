#!/bin/sh

libtoolize --force 2>&1 | sed '/^You should/d' || {
    echo "libtool failed, exiting..."
    exit 1
}

aclocal $ACLOCAL_FLAGS || {
    echo "aclocal \$ACLOCAL_FLAGS where \$ACLOCAL_FLAGS= failed, exiting..."
    exit 1
}

automake --add-missing --foreign || {
    echo "automake --add-missing --foreign failed, exiting..."
    exit 1
}

autoconf || {
    echo "autoconf failed, exiting..."
    exit 1
}

echo "Running ./configure --enable-maintainer-mode $@..."

./configure --enable-maintainer-mode $@

