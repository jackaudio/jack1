#!/bin/sh

if which libtoolize >/dev/null
then
    LIBTOOLIZE=libtoolize
else
    if which glibtoolize >/dev/null
    then
	# on the Mac it's called glibtoolize for some reason
	LIBTOOLIZE=glibtoolize
    else
	echo "libtoolize not found"
	exit 1
    fi
fi

$LIBTOOLIZE --force 2>&1 | sed '/^You should/d' || {
    echo "libtool failed, exiting..."
    exit 1
}

aclocal $ACLOCAL_FLAGS || {
    echo "aclocal \$ACLOCAL_FLAGS where \$ACLOCAL_FLAGS= failed, exiting..."
    exit 1
}

autoheader || {
    echo "autoheader failed, exiting..."
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

if test x$1 != x--no-conf; then
  echo "Running ./configure --enable-maintainer-mode $@..."
  ./configure --enable-maintainer-mode $@
fi
