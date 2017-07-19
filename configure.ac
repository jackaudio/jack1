dnl Process this file with autoconf to produce a configure script.
dnl $Id$

AC_INIT([jack-audio-connection-kit],[0.125.0rc1],
                [jack-devel@lists.jackaudio.org],[],[http://www.jackaudio.org/])

AC_CONFIG_SRCDIR([jackd/jackd.c])

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR(config)
AC_LANG([C])

dnl
dnl Check for existing JACK installs
dnl

AC_ARG_ENABLE(force-install,
	AC_HELP_STRING([--enable-force-install],
    [force installation when another Jack is found]),
	[FORCE_INSTALL=$enableval])

AC_MSG_CHECKING([existing, conflicting JACK installs])
not_overwriting=0
installs=
for dir in /usr/lib /usr/local/lib /opt/lib ; do
    if test -d $dir ; then
	if test $(find $dir/ -name 'libjack.so.*' 2>/dev/null | wc -l) -gt 0 ; then
	    if echo $prefix/lib | grep -vs $dir >/dev/null 2>&1 ; then
		not_overwriting=$(expr $not_overwriting + 1)
	    fi
	    installs="$installs $dir"
	fi
    fi
done

if test "x$FORCE_INSTALL" != "xyes" -a $not_overwriting -gt 0 ; then
    AC_MSG_RESULT(yes)
    echo
    echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    echo "You appear to have at least one existing installation of JACK."
    echo
    echo "Complete or partial JACK installs exist in:$installs"
    echo
    echo "Installing this version will leave at least one of these"
    echo "existing installations installed and this will probably break"
    echo "JACK on your machine. "
    echo
    echo "Before building, you should first remove the existing JACK"
    echo "installation(s). "
    echo
    echo "Alternatively use ./configure --enable-force-install to force"
    echo "overwriting the existing install."
    echo
    echo "WARNING: ON ANY DEBIAN-DERIVED DISTRIBUTION (Debian, Ubuntu etc)"
    echo "CHANGING THE INSTALLATION PREFIX WILL NOT PRODUCE A WORKING JACK"
    echo "INSTALL. Please contact the distribution packager for JACK and"
    echo "ask them to fix their packaging."
    echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
    if test "x$FORCE_INSTALL" != "xyes"; then
        exit 1
    fi
fi
AC_MSG_RESULT(no)

AC_CONFIG_AUX_DIR(config)
AC_CANONICAL_BUILD

dnl ---
dnl HOWTO: updating the JACK version number
dnl
dnl major version = ask on jackit-devel :)
dnl minor version = incremented when any of the public or internal
dnl                 interfaces are changed
dnl micro version = incremented when implementation-only
dnl                 changes are made
dnl ---

JACK_MAJOR_VERSION=$(echo $PACKAGE_VERSION | sed "s/\..*//")
JACK_MINOR_VERSION=$(echo $PACKAGE_VERSION | sed "s/${JACK_MAJOR_VERSION}\.//;s/\..*//")
JACK_MICRO_VERSION=$(echo $PACKAGE_VERSION | sed "s/.*\.//g")

if test "${JACK_MAJOR_VERSION}.${JACK_MINOR_VERSION}.${JACK_MICRO_VERSION}" != "${PACKAGE_VERSION}" ; then
	echo "ooops"
	echo "Found : ${JACK_MAJOR_VERSION}.${JACK_MINOR_VERSION}.${JACK_MICRO_VERSION}"
	echo "Expected : ${PACKAGE_VERSION}"
	exit 1
	fi

dnl ---
dnl HOWTO: updating the jack protocol version
dnl
dnl increment the protocol version whenever a change is
dnl made to the way libjack communicates with jackd
dnl that would break applications statically linked with an older
dnl version of libjack. NOTE: statically linking to libjack
dnl is a huge mistake.
dnl ---
JACK_PROTOCOL_VERSION=25

dnl ---
dnl HOWTO: updating the libjack interface version
dnl
dnl current = incremented whenever the public libjack API is changed
dnl revision = incremented when the libjack implementation is changed
dnl age = current libjack is both source and binary compatible with
dnl       libjack interfaces current,current-1,...,current-age
dnl
dnl Note! see libtool documentation for detailed documentation
dnl
dnl Note Also! until we reach CURRENT>=1, the rules are somewhat
dnl slacker than this, and closer to those for the JACK version
dnl number.
dnl ---
JACK_API_CURRENT=0
JACK_API_REVISION=28
JACK_API_AGE=0

AC_SUBST(JACK_PROTOCOL_VERSION)
AC_DEFINE_UNQUOTED(PROTOCOL_VERSION, "$JACK_PROTOCOL_VERSION", [Protocol version])

AC_SUBST(JACK_API_MAJOR_VERSION)
AC_SUBST(JACK_API_MINOR_VERSION)
AC_SUBST(JACK_API_MICRO_VERSION)

JACK_SO_VERSION=${JACK_API_CURRENT}:${JACK_API_REVISION}:${JACK_API_AGE}

JACK_VERSION=$JACK_MAJOR_VERSION.$JACK_MINOR_VERSION.${JACK_MICRO_VERSION}${BETA}
JACK_RELEASE=$JACK_MAJOR_VERSION-$JACK_MINOR_VERSION-${JACK_MICRO_VERSION}${BETA}


AC_SUBST(JACK_SO_VERSION)
dnl JACK_VERSION is needed for doxygen
AC_SUBST(JACK_VERSION)
AC_SUBST(JACK_RELEASE)

AM_INIT_AUTOMAKE
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

dnl
dnl save any user-provided CFLAGS so we can forget
dnl about any nonsense that configure generates
dnl

ORIGINAL_CFLAGS="$CFLAGS"

AM_CONFIG_HEADER(config.h)
AC_ENABLE_STATIC(no)
AC_ENABLE_SHARED(yes)
AC_PROG_CC
AC_PROG_CXX
AC_PROG_LD
AM_PROG_LIBTOOL
AC_PROG_LN_S
AM_PROG_CC_C_O
AC_C_BIGENDIAN

dnl
dnl now use those user-provided CFLAGS
dnl and dump whatever nonsense configure generated
dnl while looking for a compiler
dnl

CFLAGS="$ORIGINAL_CFLAGS"

AC_MSG_CHECKING([platform dependencies])

HOST_DEFAULT_TMP_DIR=/dev/shm
USE_MD5SUM=1
case "${host_os}" in
  freebsd*)
    # current FreeBSD header files conflict with the OSS driver's
    # barrier code, this may be fixed in 5.3, stay tuned.
    USE_BARRIER="no"
    USE_MD5SUM=0
    ;;
  openbsd*)
    # pthread_barrier* not implemented
    USE_BARRIER="no"
    USE_MD5SUM=0
    # need small realtime stack
    JACK_THREAD_STACK_TOUCH=10000
    ;;
  darwin*)
    JACK_THREAD_STACK_TOUCH=10000	# need small realtime stack
    JACK_CPP_VARARGS_BROKEN=1
    JACK_DO_NOT_MLOCK=1
    JACK_USE_MACH_THREADS=1
    OS_LDFLAGS="-framework CoreAudio -framework CoreServices -framework AudioUnit"
    TRY_POSIX_SHM=yes			# POSIX shm works better
    HOST_DEFAULT_TMP_DIR=/tmp
    ;;
esac

AC_SUBST(OS_LDFLAGS)
AC_SUBST(USE_MD5SUM)
AC_DEFINE_UNQUOTED(USE_MD5SUM,"$USE_MD5SUM",[Using md5sum command line if available])
AM_CONDITIONAL(USE_MD5SUM, $USE_MD5SUM)

# system-dependent config.h values
test "x$JACK_THREAD_STACK_TOUCH" = "x" && JACK_THREAD_STACK_TOUCH=500000
AC_DEFINE_UNQUOTED(JACK_THREAD_STACK_TOUCH,
	[$JACK_THREAD_STACK_TOUCH],
	[Guaranteed size of realtime stack])
if test "x$JACK_CPP_VARARGS_BROKEN" != "x"; then
    AC_DEFINE_UNQUOTED(JACK_CPP_VARARGS_BROKEN,
	[$JACK_CPP_VARARGS_BROKEN],
	[CPP has a broken varargs implementation])
fi
if test "x$JACK_USE_MACH_THREADS" != "x"; then
    AC_DEFINE_UNQUOTED(JACK_USE_MACH_THREADS,
	[$JACK_USE_MACH_THREADS],
	[Use MACH threads where possible])
fi

# headers
AC_CHECK_HEADERS(string.h strings.h db.h, [],
     AC_MSG_ERROR([*** a required header file is missing]))

AC_CHECK_HEADERS(getopt.h, [], [
    for d in /Developer/SDKs/MacOSX10.3.0.sdk/usr/include/ ; do
	AC_CHECK_HEADERS($d/getopt.h, [], [CFLAGS="$CFLAGS -I$d"])
    done])
AC_CHECK_HEADER(/usr/include/nptl/pthread.h,
	[CFLAGS="$CFLAGS -I/usr/include/nptl"])

# functions and libraries
AC_CHECK_FUNC(getopt_long, [],
    AC_MSG_ERROR([*** JACK requires GNU getopt_long]))
AC_CHECK_FUNC(gethostent, [], AC_CHECK_LIB(nsl, gethostent))
AC_CHECK_FUNC(setsockopt, [], AC_CHECK_LIB(socket, setsockopt))
AC_CHECK_FUNC(connect, [], AC_CHECK_LIB(inet, connect))
AC_CHECK_FUNC(dlopen, [],
    AC_CHECK_LIB(dl, dlopen, [],
	AC_MSG_ERROR([*** JACK requires dynamic load support])))
AC_CHECK_FUNC(pthread_create, [],
    AC_CHECK_LIB(pthread, pthread_create, [],
	AC_MSG_ERROR([*** JACK requires POSIX threads support])))
AC_CHECK_FUNCS(on_exit atexit)
AC_CHECK_FUNCS(posix_memalign)
AC_CHECK_LIB(m, sin)
AC_CHECK_LIB(db, db_create,[],
	 AC_MSG_ERROR([*** JACK requires Berkeley DB libraries (libdb...)]))

echo -n "Checking for ppoll()... "
AC_EGREP_CPP( ppoll,
[
#define _GNU_SOURCE
#include <poll.h>
], [
    	AC_DEFINE(HAVE_PPOLL,1,"Whether ppoll is available")
	echo "yes"
   ],[
    	AC_DEFINE(HAVE_PPOLL,0,"Whether ppoll is available")
	echo "no"
   ] )



AC_CHECK_FUNC(clock_gettime,
       [
           AC_DEFINE(HAVE_CLOCK_GETTIME,1,"Whether or not clock_gettime can be found in system libraries")
       ],
       #
       # if not found, check librt specifically
       #
       AC_CHECK_LIB(rt, clock_gettime,
           [
              AC_DEFINE(HAVE_CLOCK_GETTIME,1,"Whether or not clock_gettime can be found in system libraries (via -lrt)")
	      OS_LDFLAGS="$OS_LDFLAGS -lrt"
	   ])
)

AC_CHECK_FUNC(clock_nanosleep,
       [
           AC_DEFINE(HAVE_CLOCK_NANOSLEEP,1,"Whether or not clock_nanosleep can be found in system libraries")
       ],
       #
       # if not found, check librt specifically
       #
       AC_CHECK_LIB(rt, clock_nanosleep,
           [
              AC_DEFINE(HAVE_CLOCK_NANOSLEEP,1,"Whether or not clock_nanosleep can be found in system libraries (via -lrt)")
	      OS_LDFLAGS="$OS_LDFLAGS -lrt"
	   ])
)

# should we use mlockall() on this platform?
if test "x$JACK_DO_NOT_MLOCK" = "x"; then
    AC_CHECK_HEADER(sys/mman.h,
        [AC_CHECK_FUNC(mlockall,
	    [AC_DEFINE(USE_MLOCK, 1, [Use POSIX memory locking])])])
fi

# look for system support for POSIX shm API
AC_ARG_ENABLE(posix-shm,
	AC_HELP_STRING([--enable-posix-shm], [use POSIX shm API (default=auto)]),
	[TRY_POSIX_SHM=$enableval])
if test "x$TRY_POSIX_SHM" = "xyes"
then
	AC_CHECK_FUNC(shm_open, [],
		AC_CHECK_LIB(rt, shm_open, [], [TRY_POSIX_SHM=no]))
fi
AC_MSG_CHECKING([shared memory support])
if test "x$TRY_POSIX_SHM" = "xyes"
then
	AC_MSG_RESULT([POSIX shm_open().])
	AC_DEFINE(USE_POSIX_SHM,1,[Use POSIX shared memory interface])
	JACK_SHM_TYPE='"POSIX"'
	USE_POSIX_SHM="true"
else
	AC_MSG_RESULT([System V shmget().])
	JACK_SHM_TYPE='"System V"'
	USE_POSIX_SHM="false"
fi
AC_DEFINE_UNQUOTED(JACK_SHM_TYPE, [$JACK_SHM_TYPE],
	[JACK shared memory type])
AM_CONDITIONAL(USE_POSIX_SHM, $USE_POSIX_SHM)

JACK_CORE_CFLAGS="-I\$(top_srcdir)/config -I\$(top_srcdir) \
-I\$(top_srcdir)/include -I\$(top_builddir)/include \
-D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS -Wall"

JACK_LIBC_HELPER_FLAGS=
AC_ARG_ENABLE(ancient_libc,
	AC_HELP_STRING([--enable-ancient-libc],[Add required CFLAGS for libc versions too old to expose PThread R/W Lock and other modern code]),
	JACK_LIBC_HELPER_FLAGS="-D_XOPEN_SOURCE=600 -D_BSD_SOURCE",
	JACK_LIBC_HELPER_FLAGS=""
	)

JACK_CORE_CFLAGS="$JACK_CORE_CFLAGS $JACK_LIBC_HELPER_FLAGS"

case $build_os in
     # we need weak linkage which appeared in 10.2, but lets ask for 10.4 anyway
     darwin*) JACK_CORE_CFLAGS="$JACK_CORE_CFLAGS -mmacosx-version-min=10.4" ;;
esac

AC_ARG_WITH(cpu-target,
            [  --with-cpu-target=cpu-type    explicit, overriding argument for gcc -march= flag])


dnl
dnl figure out how best to optimize
dnl JOQ: this should be done via config/configure.hosts
dnl

dnl ----------------------------------------------------------------------

if test "$build_cpu" = "powerpc64" ; then

    AC_ARG_ENABLE(cell,
	AC_HELP_STRING([--enable-cell],[enable Cell BE support (default=no)]),,
	[ enable_cell=no ])

    if test "x$enable_cell" = xyes; then
        AC_DEFINE(ENABLE_CELLBE, 1, [Define to 1 if you need support for the Cell BE.])
    fi

fi

if test "build_cpu" = "powerpc" -o "$build_cpu" = "powerpc64" ; then

    AC_DEFINE(POWERPC, 1, "Are we running a ppc CPU?")

############################
# Check for Altivec assembly
############################

    AC_ARG_ENABLE(altivec,
	AC_HELP_STRING([--enable-altivec],[enable Altivec support (default=auto)]),,
	[ enable_altivec=yes ])

    if test "x$enable_altivec" = xyes; then

	AC_MSG_CHECKING(whether we can compile Altivec code)

	AC_COMPILE_IFELSE([AC_LANG_SOURCE([asm ("vand %v0, %v0, %v0");])],
	    AC_DEFINE(USE_ALTIVEC, 1, [Define to 1 if Altivec assembly is available.])
	    AC_DEFINE(HAVE_ALTIVEC_LINUX, 1, "Is there Altivec Support ?")
	    ALTIVEC_OPT_FLAGS="-maltivec -mabi=altivec"
	    AC_MSG_RESULT(yes)
	    ,
	    enable_altivec=no
	    AC_MSG_RESULT(no)
	    AC_MSG_WARN([The assembler does not support the Altivec command set.])
	)

    fi

    dnl -mcpu=7450 does not reliably work with gcc 3.*

    JACK_OPT_CFLAGS="-O2 -mcpu=7400 $ALTIVEC_OPT_FLAGS -mhard-float -mpowerpc-gfxopt"

    if test "$build_cpu" = "powerpc64"; then
	JACK_OPT_CFLAGS="-O2 -mcpu=powerpc64 $ALTIVEC_OPT_FLAGS -mhard-float"
    fi

elif echo $build_cpu | egrep '(i.86|x86_64)' >/dev/null; then

    dnl Check for SSE usability

    if test -r /proc/cpuinfo ; then
	procflags=`grep '^flags' /proc/cpuinfo`
	if echo $procflags | grep -s sse ; then
	    cpu_supports_sse=yes
	fi
    else
	# this is not linux, but assume that if the processor
	# is x86 then is supports SSE
        cpu_supports_sse=yes
	AC_MSG_WARN([Assuming your x86/x86_64 system can support SSE. Use --disable-sse if this is not the case])
    fi

    AC_ARG_ENABLE(optimization-by-compiler,
	AC_HELP_STRING([--enable-optimization-by-compiler],[use compiler (NOT processor) capabilities to determine optimization flags (default=no)]),
	optimization_by_compiler=yes,
	optimization_by_compiler=no
    )

    AC_ARG_ENABLE(optimization-by-cpu,
	AC_HELP_STRING([--enable-optimization-by-cpu],[use processor capabilities to determine optimization flags (default=yes)]),,
	optimization_by_cpu=yes
    )

    AC_ARG_ENABLE(sse,
	AC_HELP_STRING([--enable-sse],[enable SSE support (default=auto)]),,
	enable_sse=yes)


    if test "x$enable_sse" = xyes; then

	AC_MSG_CHECKING(whether we can compile SSE code)

	AC_COMPILE_IFELSE([AC_LANG_SOURCE([asm ("movntps %xmm0, 0");])],
	    [
		if test x$optimization_by_cpu = xyes ; then
		    if test x$cpu_supports_sse = xyes ; then
			SSE_FLAGS="-msse -mfpmath=sse"
		    fi
		else
		    SSE_FLAGS="-msse -mfpmath=sse"
		fi

		AC_MSG_RESULT(yes)
		],
	    [
		enable_sse=no
		AC_MSG_RESULT(no)
		AC_MSG_WARN([The assembler does not support the SSE command set.])
		])
    fi

    AC_DEFINE(x86, 1, "Nope it's intel")
    COMMON_X86_OPT_FLAGS="-O3 -fomit-frame-pointer -ffast-math -funroll-loops"

dnl
dnl its a little sad that OS X doesn't make it possible to identify
dnl the build_cpu a little more precisely. on os x we always get "i386"
dnl as the CPU type. we miss out on some possible optimizations as
dnl a result. oh well.
dnl

    if test x$with_cpu_target != x ; then
        JACK_OPT_CFLAGS="-march=$with_cpu_target -mtune=$with_cpu_target"
    else
        case ${host_os} in
	    darwin*) # apple gcc, not GNU
	             JACK_OPT_CFLAGS="-mtune=generic" ;;
	          *) # any other gcc
		     JACK_OPT_CFLAGS="-march=native -mtune=native" ;;
	esac
    fi

dnl
dnl do not add h/w specific flags if asked to let compiler
dnl to the optimization
dnl

    if test x$optimization_by_compiler != xyes ; then
        JACK_OPT_CFLAGS="$COMMON_X86_OPT_FLAGS $JACK_OPT_CFLAGS $SSE_FLAGS"
    else
       JACK_OPT_CFLAGS="$COMMON_X86_OPT_FLAGS"
    fi

fi

AC_ARG_ENABLE(dynsimd,
	AC_HELP_STRING([--enable-dynsimd],[enable dynamic SIMD selection (default=no)]),,
    enable_dynsimd=no)

if test "x$enable_dynsimd" = xyes; then
	AC_DEFINE(USE_DYNSIMD, 1, [Define to 1 to use dynamic SIMD selection.])
	dnl This needs update once there's more than x86/x86-64 supported
	SIMD_CFLAGS="-O -msse -msse2 -m3dnow"
	AC_SUBST(SIMD_CFLAGS)
fi

AC_ARG_ENABLE(optimize,
    AC_HELP_STRING([--enable-optimize],
	[optimize code, based on CPU or compiler, as separately selected (default=no)]),
    [ if test x$enable_optimize != xno ; then
	AC_MSG_WARN([optimization in use.........................])
      else
        # no optimization, so lets get debugging symbols instead
	JACK_OPT_CFLAGS="-g"
	AC_MSG_WARN([no optimization.........................])
      fi
    ],
    [
        # no optimization, so lets get debugging symbols instead
	JACK_OPT_CFLAGS="-g"
	AC_MSG_WARN([no optimization.........................])
    ]
)

JACK_CFLAGS="$JACK_CORE_CFLAGS $JACK_OPT_CFLAGS"

dnl
dnl most makefiles use JACK_CFLAGS, but simd code needs JACK_CORE_CFLAGS
dnl

AC_SUBST(JACK_CFLAGS)
AC_SUBST(JACK_CORE_CFLAGS)

dnl
dnl use JACK_CFLAGS for jackd compilation
dnl

CFLAGS="$CFLAGS $JACK_CFLAGS"

# allow buffer resizing unless --disable-resize specified
buffer_resizing=yes
AC_ARG_ENABLE(resize,
	AC_HELP_STRING([--enable-resize], [enable buffer resizing feature (default=yes)]),
        [
	  if test x$enable_resize = xno ; then
	  	buffer_resizing=no
	  fi
	 ]
)

if test x$buffer_resizing != xno; then
   AC_DEFINE(DO_BUFFER_RESIZE,,[Enable buffer resizing])
fi

AC_ARG_ENABLE(valgrind-clean,
	AC_HELP_STRING([--enable-valgrind-clean],[spend a few extra CPU cycles avoiding unnecessary valgrind warnings (default=no)]),
        [
	  if test x$enable_valgrind_clean != xno ; then
	     AC_DEFINE(VALGRIND_CLEAN,,[clean up for valgrind])
          fi
        ]
)

AC_ARG_ENABLE(debug,
	AC_HELP_STRING([--enable-debug],
		[enable debugging messages in jackd and libjack (default=no)]),
        [
	  if test x$enable_debug != xno ; then
	     AC_DEFINE(DEBUG_ENABLED,,[Enable debugging messages])
          fi
        ]
)

AC_ARG_ENABLE(timestamps,
	AC_HELP_STRING([--enable-timestamps],
		[allow clients to use the JACK timestamp API (JACK developers only) (default=no)]),
        [
	  if test x$enable_timestamps != xno ; then
	  	AC_DEFINE(WITH_TIMESTAMPS,,[Enable JACK timestamp API])
          fi
        ]
)

AC_ARG_ENABLE(preemption-check,
	AC_HELP_STRING([--enable-preemption-check],
		[check for inappropriate realtime preemption (requires a specially built Linux kernel) (default=no)]),
		[
		  if test x$enable_preemption_check != xno ; then
			echo checking for realtime preemption bugs
			AC_DEFINE(DO_PREEMPTION_CHECKING,,
				[check realtime preemption])
		  fi
		])

USE_CAPABILITIES=false

AC_ARG_ENABLE(capabilities,
	AC_HELP_STRING([--enable-capabilities],[!!! LINUX 2.4 KERNELS ONLY !!! use libcap to gain realtime scheduling priviledges]),
              [ if test "x$enable_capabilities" != "xno" ; then
                    AC_CHECK_LIB(cap, capgetp,
                                 [AC_CHECK_HEADER(sys/capability.h,
                                                  [HAVE_CAPABILITIES=true],
                                                  [AC_MSG_WARN([*** no kernel support for capabilities])
                                                   HAVE_CAPABILITIES=false]
                                  )],
                                 [AC_MSG_WARN([*** no libcap present])
                                  HAVE_CAPABILITIES=false]
                    )
                    if test "x$HAVE_CAPABILITIES" = "xtrue"; then
                        AC_CHECK_PROG(HAVE_CAPABILITIES, md5sum, true, false)
                    fi
                    if test "x$HAVE_CAPABILITIES" = "xfalse"; then
                        AC_MSG_WARN([*** required program md5sum not found])
                    fi
                    if test "x$HAVE_CAPABILITIES" = "xfalse"; then
                        AC_MSG_ERROR([*** Capabilities support not present. Run configure again without --enable-capabilities.])
                    fi
                    USE_CAPABILITIES=true
                    AC_DEFINE(USE_CAPABILITIES,,[Enable POSIX 1.e capabilities support])
                fi
              ]
)

with_oldtrans=yes
AC_ARG_ENABLE(oldtrans,
	AC_HELP_STRING([--disable-oldtrans],[remove old transport interfaces (default=yes)]),
              [ if test "x$enable_oldtrans" = "xno" ; then
		    with_oldtrans=no
		fi
	      ]
)
if test "x$with_oldtrans" != "xno" ; then
	AC_DEFINE(OLD_TRANSPORT,,[Include old transport interfaces])
fi

STRIPPED_JACKD=false
AC_ARG_ENABLE(stripped-jackd,
	AC_HELP_STRING([--enable-stripped-jackd],[strip jack before computing its md5 sum (useful only with Linux 2.4 kernel)]),
              [ if test "x$USE_CAPABILITIES" != "xtrue" ; then
                    AC_MSG_WARN([*** capabilities not enabled, stripped jackd has no effect])
                elif test "x$enable_stripped_jackd" != "xno"; then
                    STRIPPED_JACKD=true
                fi
              ]
)

# plugins go in the addon dir.

ADDON_DIR='${libdir}/jack'
AC_SUBST(ADDON_DIR)
AS_AC_EXPAND(ADDON_DIR_EXPANDED,${libdir}/jack)
AC_DEFINE_UNQUOTED(ADDON_DIR,"$ADDON_DIR_EXPANDED",[Directory for plugins])

AC_ARG_WITH(html-dir,
		AC_HELP_STRING([--with-html-dir=PATH],[where to install the html documentation]))

if test "x$with_html_dir" = "x" ; then
    HTML_DIR='${pkgdatadir}'
else
    HTML_DIR=$with_html_dir
fi

AC_SUBST(HTML_DIR)


# allow specifying default tmpdir
AC_ARG_WITH(default-tmpdir,
            AC_HELP_STRING([--with-default-tmpdir],[where jackd and clients will put tmp files (default=/dev/shm)]))

if test "x$with_default_tmpdir" = "x" ; then
	DEFAULT_TMP_DIR=$HOST_DEFAULT_TMP_DIR
else
	DEFAULT_TMP_DIR=$with_default_tmpdir
fi

AC_SUBST(DEFAULT_TMP_DIR)
AC_DEFINE_UNQUOTED(DEFAULT_TMP_DIR,"$DEFAULT_TMP_DIR",[Default tmp directory])

# Check for barrier functions in the pthreads library.  The default
# option setting may be OS-dependent, otherwise it's "yes".
test "x$USE_BARRIER" = "x" && USE_BARRIER="yes"
AC_ARG_WITH(barrier,
	AC_HELP_STRING([--without-barrier],
		       [avoid using pthread barrier functions (only used by OSS driver)]),
	[ USE_BARRIER=$withval ])
if test "x$USE_BARRIER" = "xyes"; then
	AC_CHECK_LIB([pthread], [pthread_barrier_init],
		AC_DEFINE(USE_BARRIER, 1, [Use pthread barrier functions]))
fi

# some example-clients need libsndfile
HAVE_SNDFILE=false
PKG_CHECK_MODULES(SNDFILE, sndfile >= 1.0,[HAVE_SNDFILE=true], [true])
if test x$HAVE_SNDFILE = xfalse; then
             AC_MSG_WARN([*** the jackrec example client will not be built])
fi

# NetJack backend and internal client need libsamplerate
HAVE_SAMPLERATE=false
PKG_CHECK_MODULES(SAMPLERATE, samplerate >= 0.1.2,[HAVE_SAMPLERATE=true], [true])
if test x$HAVE_SAMPLERATE = xfalse; then
        AC_MSG_WARN([*** the NetJack backend and internal client will not be built])
    	AC_DEFINE(HAVE_SAMPLERATE,0,"Whether libsamplerate is available")
else
	NETJACK_CFLAGS="$NETJACK_CFLAGS $SAMPLERATE_CFLAGS"
	NETJACK_LIBS="$NETJACK_LIBS $SAMPLERATE_LIBS"
    	AC_DEFINE(HAVE_SAMPLERATE,1,"Whether libsamplerate is available")
fi

# if we have Fons Adriensen's zita libs installed, we can build the zita-based
# internal clients for using additional (ALSA) devices with JACK

HAVE_ZITA_BRIDGE_DEPS=false
HAVE_ZITA_RESAMPLE=false
HAVE_ZITA_ALSA_PCMI=false

case $build_os in
     linux*)
      AC_CHECK_LIB(zita-resampler, _Z28zita_resampler_major_versionv,
            [
               HAVE_ZITA_RESAMPLE=true
               AC_MSG_RESULT(yes)
            ])
      AC_CHECK_LIB(zita-alsa-pcmi, _Z28zita_alsa_pcmi_major_versionv,
            [
               HAVE_ZITA_ALSA_PCMI=true
               AC_MSG_RESULT(yes)
            ])

     AC_ARG_ENABLE(zalsa,
           AC_HELP_STRING([--enable-zalsa],[Build clients using Fons Adriensen\'s Zita libraries for access to multiple devices (default=yes if required libraries are present)]),
           [
               if test x$enable_zalsa != xno -a x$HAVE_ZITA_RESAMPLE = xtrue -a x$HAVE_ZITA_ALSA_PCMI = xtrue ; then
                   AC_DEFINE(HAVE_ZITA_BRIDGE_DEPS,1,"Whether we have the libs needed for Zita ALSA bridge support")
                   HAVE_ZITA_BRIDGE_DEPS=true
               fi
           ],
           [
               if test x$HAVE_ZITA_RESAMPLE = xtrue -a x$HAVE_ZITA_ALSA_PCMI = xtrue ; then
                   AC_DEFINE(HAVE_ZITA_BRIDGE_DEPS,1,"Whether we have the libs needed for Zita ALSA bridge support")
                   HAVE_ZITA_BRIDGE_DEPS=true
               fi
           ]
     )
     ;;
esac
AM_CONDITIONAL(HAVE_ZITA_ALSA_PCMI, $HAVE_ZITA_ALSA_PCMI)

# Celt low-latency audio codec. netjack transmission via internet.
HAVE_CELT=false
PKG_CHECK_MODULES(CELT, celt >= 0.8.0,[HAVE_CELT=true], [true])
if test x$HAVE_CELT = xfalse; then
	PKG_CHECK_MODULES(CELT, celt >= 0.7.0,[HAVE_CELT=true], [true])
	if test x$HAVE_CELT = xfalse; then
		PKG_CHECK_MODULES(CELT, celt >= 0.5.0,[HAVE_CELT=true], [true])
		if test x$HAVE_CELT = xfalse; then
			AC_DEFINE(HAVE_CELT,0,"Whether CELT is available")
			AC_MSG_RESULT(no)
			AC_MSG_WARN([*** NetJack will not be built with celt support])
		else
			AC_DEFINE(HAVE_CELT,1,"Whether CELT is available")
			AC_DEFINE(HAVE_CELT_API_0_5,1,"Whether CELT API is 0.5 API")
			AC_DEFINE(HAVE_CELT_API_0_7,0,"Whether CELT API is 0.7 API")
			AC_DEFINE(HAVE_CELT_API_0_8,0,"Whether CELT API is 0.8 API")
			AC_MSG_RESULT(yes (0.5))
			NETJACK_LIBS="$NETJACK_LIBS $CELT_LIBS"
		fi
	else
		AC_DEFINE(HAVE_CELT,1,"Whether CELT is available")
		AC_MSG_RESULT(yes (0.7))
		AC_DEFINE(HAVE_CELT_API_0_5,0,"Whether CELT API is 0.5 API")
		AC_DEFINE(HAVE_CELT_API_0_7,1,"Whether CELT API is 0.7 API")
		AC_DEFINE(HAVE_CELT_API_0_8,0,"Whether CELT API is 0.8 API")
		NETJACK_LIBS="$NETJACK_LIBS $CELT_LIBS"
	fi
else
		AC_DEFINE(HAVE_CELT,1,"Whether CELT is available")
		AC_MSG_RESULT(yes (0.7))
		AC_DEFINE(HAVE_CELT_API_0_5,0,"Whether CELT API is 0.5 API")
		AC_DEFINE(HAVE_CELT_API_0_7,0,"Whether CELT API is 0.7 API")
		AC_DEFINE(HAVE_CELT_API_0_8,1,"Whether CELT API is 0.8 API")
		NETJACK_LIBS="$NETJACK_LIBS $CELT_LIBS"
fi

AC_SUBST(NETJACK_LIBS)
AC_SUBST(NETJACK_CFLAGS)

# Note: A bug in pkg-config causes problems if the first occurence of
# PKG_CHECK_MODULES can be disabled. So, if you're going to use
# PKG_CHECK_MODULES inside a --disable-whatever check, you need to
# do it somewhere *below* this comment.

# Check which backend drivers can be built.  The last one successfully
# configured becomes the default JACK driver; so the order of
# precedence is: alsa, sun, oss, coreaudio, portaudio, sndio, dummy.

JACK_DEFAULT_DRIVER=\"dummy\"

AC_ARG_ENABLE(portaudio,
	AC_HELP_STRING([--enable-portaudio],[build PortAudio driver]),
   TRY_PORTAUDIO=$enableval , TRY_PORTAUDIO=no)

HAVE_PA="false"
if test "x$TRY_PORTAUDIO" = "xyes"
then
	# check for portaudio V18
	AC_CHECK_LIB(portaudio, Pa_Initialize,
		[ AC_CHECK_HEADERS(portaudio.h,
			[ HAVE_PA="true"
			  PA_LIBS=-lportaudio
			  JACK_DEFAULT_DRIVER=\"portaudio\"
			])
		])
	AC_SUBST(PA_LIBS)
fi
AM_CONDITIONAL(HAVE_PA, $HAVE_PA)

AC_ARG_ENABLE(coreaudio, AC_HELP_STRING([--disable-coreaudio], [ignore CoreAudio driver]),
			TRY_COREAUDIO=$enableval , TRY_COREAUDIO=yes )
HAVE_COREAUDIO="false"
if test "x$TRY_COREAUDIO" = "xyes"
then
	# check for coreaudio
	AC_CHECK_HEADERS(CoreAudio/CoreAudio.h,
		[ HAVE_COREAUDIO="true"
		  JACK_DEFAULT_DRIVER=\"coreaudio\"
		])
fi
AM_CONDITIONAL(HAVE_COREAUDIO, $HAVE_COREAUDIO)

AC_ARG_ENABLE(oss, AC_HELP_STRING([--disable-oss],[ignore OSS driver ]),
			TRY_OSS=$enableval , TRY_OSS=yes )
HAVE_OSS="false"
USE_OSS_COOKEDMODE="false"
if test "x$TRY_OSS" = "xyes"
then
	# check for Open Sound System
	AC_CHECK_HEADER([sys/soundcard.h],
	     [HAVE_OSS="true"
              JACK_DEFAULT_DRIVER=\"oss\"])

	# enable OSS cookedmode to allow hardware with mismatching sample rates
	if test "x$HAVE_OSS" = "xtrue"
	then
		AC_ARG_ENABLE(oss-cookedmode, AC_HELP_STRING([--enable-oss-cookedmode],[Enable cooked mode in OSS to accommodate hardware with mismatching sample rate]),
				ENABLE_OSS_COOKEDMODE=$enableval, ENABLE_OSS_COOKEDMODE=no)
		if test "x$ENABLE_OSS_COOKEDMODE" = "xyes"
		then
			USE_OSS_COOKEDMODE=true
			AC_DEFINE(USE_OSS_COOKEDMODE, 1, [Enable cooked mode in OSS to accommodate hardware with mismatching sample rate])
		fi
	fi
fi
AM_CONDITIONAL(HAVE_OSS, $HAVE_OSS)
AM_CONDITIONAL(USE_OSS_COOKEDMODE, $USE_OSS_COOKEDMODE)

AC_ARG_ENABLE(sun, AC_HELP_STRING([--disable-sun],[ignore Sun driver ]),
			TRY_SUN=$enableval , TRY_SUN=yes )
HAVE_SUN="false"
if test "x$TRY_SUN" = "xyes"
then
	# check for Sun audio API
	AC_CHECK_HEADER([sys/audioio.h],
	     [HAVE_SUN="true"
              JACK_DEFAULT_DRIVER=\"sun\"])
fi
AM_CONDITIONAL(HAVE_SUN, $HAVE_SUN)

AC_ARG_ENABLE(sndio, AC_HELP_STRING([--disable-sndio],[ignore sndio driver ]),
			TRY_SNDIO=$enableval , TRY_SNDIO=yes ) 
HAVE_SNDIO="false"
if test "x$TRY_SNDIO" = "xyes"
then
	# check for sndio audio API
	AC_CHECK_HEADER([sndio.h],
	     [HAVE_SNDIO="true"
              JACK_DEFAULT_DRIVER=\"sndio\"])
	SNDIO_LIBS="-lsndio"
	AC_SUBST([SNDIO_LIBS])
fi
AM_CONDITIONAL(HAVE_SNDIO, $HAVE_SNDIO)

AC_ARG_ENABLE(freebob, AC_HELP_STRING([--disable-freebob],[ignore FreeBob driver ]),
			TRY_FREEBOB=$enableval , TRY_FREEBOB=yes )
HAVE_FREEBOB="false"
if test "x$TRY_FREEBOB" = "xyes"
then
	# check for FreeBob libraries
	PKG_CHECK_MODULES(LIBFREEBOB, libfreebob >= 1.0.0,
	     [HAVE_FREEBOB="true"
              JACK_DEFAULT_DRIVER=\"freebob\"
	     ], AC_MSG_RESULT([no]))

	AC_SUBST([LIBFREEBOB_CFLAGS])
	AC_SUBST([LIBFREEBOB_LIBS])

fi
AM_CONDITIONAL(HAVE_FREEBOB,$HAVE_FREEBOB)

AC_ARG_ENABLE(firewire, AC_HELP_STRING([--disable-firewire],[ignore FireWire driver (FFADO)]),
			TRY_FIREWIRE=$enableval , TRY_FIREWIRE=yes )
HAVE_FIREWIRE="false"
if test "x$TRY_FIREWIRE" = "xyes"
then
	# check for FFADO libraries
	PKG_CHECK_MODULES(LIBFFADO, libffado >= 1.999.17,
	     [HAVE_FIREWIRE="true"
              JACK_DEFAULT_DRIVER=\"firewire\"
	     ], AC_MSG_RESULT([no]))

	AC_SUBST([LIBFFADO_CFLAGS])
	AC_SUBST([LIBFFADO_LIBS])

fi
AM_CONDITIONAL(HAVE_FIREWIRE,$HAVE_FIREWIRE)

AC_ARG_ENABLE(alsa, AC_HELP_STRING([--disable-alsa],[ignore ALSA driver ]),
			TRY_ALSA=$enableval , TRY_ALSA=yes )
HAVE_ALSA="false"
if test "x$TRY_ALSA" = "xyes"
then
	# check for ALSA >= 1.0.18
	PKG_CHECK_MODULES(ALSA, alsa >= 1.0.18,
	     [HAVE_ALSA="true"
              ALSA_LIBS=-lasound
              JACK_DEFAULT_DRIVER=\"alsa\"
             ], AC_MSG_RESULT([no - cannot find ALSA 1.0.18 or later]), [-lm]
	)
	AC_SUBST(ALSA_LIBS)
fi
AM_CONDITIONAL(HAVE_ALSA, $HAVE_ALSA)

HAVE_ALSA_MIDI=$HAVE_ALSA
AM_CONDITIONAL(HAVE_ALSA_MIDI, $HAVE_ALSA_MIDI)

if test "$JACK_DEFAULT_DRIVER" = \"dummy\"; then
  AC_MSG_WARN([Only the dummy driver can be built])
fi
AC_DEFINE_UNQUOTED(JACK_DEFAULT_DRIVER,
	[$JACK_DEFAULT_DRIVER],
	[Default JACK driver])

JACK_SEMAPHORE_KEY=0x282929
AC_DEFINE_UNQUOTED(JACK_SEMAPHORE_KEY,
	  [$JACK_SEMAPHORE_KEY],
	  [ an integer constant used as the semaphore and SysV SHM key. see libjack/shm.c for usage])
AC_SUBST(JACK_SEMAPHORE_KEY)

# On some systems, readline depends on termcap or ncurses.  But, the
# MacOSX linker complains bitterly if these libraries are explicitly
# referenced.
#
# AC_CHECK_LIB() foolishly assumes that checking a library for an entry
# point always returns the same result regardless of any dependent
# libraries specified.  The `unset ac_cv_lib_readline_readline' erases
# the cached result to work around this problem.
READLINE_DEPS=""
HAVE_READLINE=true
AC_CHECK_LIB(readline, readline, [:],
    [unset ac_cv_lib_readline_readline
     AC_CHECK_LIB(readline, readline, [READLINE_DEPS="-ltermcap"],
        [unset ac_cv_lib_readline_readline
	 AC_CHECK_LIB(readline, readline,
	    [READLINE_DEPS="-lncurses"], [HAVE_READLINE=false], "-lncurses")],
	"-ltermcap")])
AC_CHECK_HEADER(readline/chardefs.h, [], [HAVE_READLINE=false])
if test x$HAVE_READLINE = xfalse; then
	AC_MSG_WARN([*** the jack_transport example client will not be built])
fi
AC_SUBST(READLINE_DEPS)

# you need doxygen to make dist.
AC_CHECK_PROG(HAVE_DOXYGEN, doxygen, true, false)
if test $HAVE_DOXYGEN = "false"; then
             AC_MSG_WARN([*** doxygen not found, docs will not be built])
fi

AM_CONDITIONAL(HAVE_SNDFILE, $HAVE_SNDFILE)
AM_CONDITIONAL(HAVE_CELT, $HAVE_CELT)
AM_CONDITIONAL(HAVE_SAMPLERATE, $HAVE_SAMPLERATE)
AM_CONDITIONAL(HAVE_READLINE, $HAVE_READLINE)
AM_CONDITIONAL(HAVE_DOXYGEN, $HAVE_DOXYGEN)
AM_CONDITIONAL(USE_CAPABILITIES, $USE_CAPABILITIES)
AM_CONDITIONAL(STRIPPED_JACKD, $STRIPPED_JACKD)
AM_CONDITIONAL(HAVE_PPOLL, $HAVE_PPOLL)
AM_CONDITIONAL(HAVE_ZITA_BRIDGE_DEPS, $HAVE_ZITA_BRIDGE_DEPS)

AC_OUTPUT(
Makefile
config/Makefile
config/os/Makefile
config/os/generic/Makefile
config/os/gnu-linux/Makefile
config/os/macosx/Makefile
config/sysdeps/Makefile
doc/Makefile
doc/reference.doxygen
drivers/Makefile
drivers/alsa/Makefile
drivers/alsa_midi/Makefile
drivers/dummy/Makefile
drivers/oss/Makefile
drivers/sun/Makefile
drivers/sndio/Makefile
drivers/portaudio/Makefile
drivers/coreaudio/Makefile
drivers/freebob/Makefile
drivers/firewire/Makefile
drivers/netjack/Makefile
example-clients/Makefile
tools/Makefile
tools/zalsa/Makefile
man/Makefile
jack.pc
jack.spec
jackd/Makefile
jackd/jackd.1
include/version.h
include/Makefile
libjack/Makefile
python/Makefile
)

dnl
dnl Output summary message
dnl

echo
echo $PACKAGE_NAME $PACKAGE_VERSION :
echo
echo \| Build with ALSA support............................... : $HAVE_ALSA
echo \| Build with old FireWire \(FreeBob\) support............. : $HAVE_FREEBOB
echo \| Build with new FireWire \(FFADO\) support............... : $HAVE_FIREWIRE
echo \| Build with OSS support................................ : $HAVE_OSS
echo \| Enable OSS COOKEDMODE for hardware flexibility........ : $USE_OSS_COOKEDMODE
echo \| Build with Sun audio support.......................... : $HAVE_SUN
echo \| Build with Sndio audio support........................ : $HAVE_SNDIO
echo \| Build with CoreAudio support.......................... : $HAVE_COREAUDIO
echo \| Build with PortAudio support.......................... : $HAVE_PA
echo \| Build with Celt support............................... : $HAVE_CELT
echo \| Build with dynamic buffer size support................ : $buffer_resizing
echo \| Build with ZITA ALSA bridge support................... : $HAVE_ZITA_BRIDGE_DEPS
echo \| Compiler optimization flags........................... : $JACK_OPT_CFLAGS
echo \| Compiler full flags................................... : $CFLAGS
echo \| Install dir for libjack + backends.................... : $libdir/jack
echo \|
echo \| Default driver backend................................ : $JACK_DEFAULT_DRIVER
echo \| Shared memory interface............................... : $JACK_SHM_TYPE
echo \| IPC Temporary directory............................... : $DEFAULT_TMP_DIR
echo \| Install prefix........................................ : $prefix
echo \| Default tmp dir....................................... : $DEFAULT_TMP_DIR
echo



