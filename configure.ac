dnl Process this file with autoconf to produce a configure script.
dnl $Id$

AC_INIT(jackd/jackd.c)

AC_CONFIG_AUX_DIR(config)
AC_CANONICAL_TARGET

dnl ---
dnl HOWTO: updating the JACK version number
dnl
dnl major version = ask on jackit-devel :)
dnl minor version = incremented when any of the public or internal
dnl                 interfaces are changed
dnl micro version = incremented when implementation-only
dnl                 changes are made
dnl ---
JACK_MAJOR_VERSION=0
JACK_MINOR_VERSION=116
JACK_MICRO_VERSION=2

dnl ---
dnl HOWTO: updating the jack protocol version
dnl
dnl increment the protocol version whenever a change is
dnl made to the way libjack communicates with jackd
dnl that would break applications statically linked with an older
dnl version of libjack. NOTE: statically linking to libjack
dnl is a huge mistake.
dnl ---
JACK_PROTOCOL_VERSION=24

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

AC_SUBST(JACK_MAJOR_VERSION)
AC_SUBST(JACK_MINOR_VERSION)
AC_SUBST(JACK_MICRO_VERSION)

AC_SUBST(JACK_PROTOCOL_VERSION)
AC_DEFINE_UNQUOTED(PROTOCOL_VERSION, "$JACK_PROTOCOL_VERSION", [Protocol version])

AC_SUBST(JACK_API_MAJOR_VERSION)
AC_SUBST(JACK_API_MINOR_VERSION)
AC_SUBST(JACK_API_MICRO_VERSION)

JACK_SO_VERSION=${JACK_API_CURRENT}:${JACK_API_REVISION}:${JACK_API_AGE}

JACK_VERSION=$JACK_MAJOR_VERSION.$JACK_MINOR_VERSION.${JACK_MICRO_VERSION}${BETA}
JACK_RELEASE=$JACK_MAJOR_VERSION-$JACK_MINOR_VERSION-${JACK_MICRO_VERSION}${BETA}

AC_SUBST(JACK_SO_VERSION)
AC_SUBST(JACK_VERSION)
AC_SUBST(JACK_RELEASE)

AM_INIT_AUTOMAKE(jack-audio-connection-kit,${JACK_VERSION})
AM_MAINTAINER_MODE

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
case "${host_os}" in
  freebsd*)
    # current FreeBSD header files conflict with the OSS driver's
    # barrier code, this may be fixed in 5.3, stay tuned.
    USE_BARRIER="no"
    ;;
  openbsd*)
    # pthread_barrier* not implemented
    USE_BARRIER="no"
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

#
# We need to establish suitable defaults for a 64-bit OS
libnn=lib
case "${host_os}" in
    linux*)
    case "${host_cpu}" in
	x86_64|mips64|ppc64|sparc64|s390x)
        libnn=lib64
	;;
    esac
    ;;
    solaris*)
    ## libnn=lib/sparcv9 ## on 64-bit only, but that's compiler-specific
    ;;  
esac

## take care not to  override the command-line setting
if test "${libdir}" = '${exec_prefix}/lib'; then
  libdir='${exec_prefix}/'${libnn}
fi

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
AC_CHECK_HEADERS(string.h strings.h alloca.h)
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
           AC_DEFINE(HAVE_CLOCK_GETTIME,0,"Whether or not clock_gettime can be found in system libraries")
       ], 
       #
       # if not found, check librt specifically
       #
       AC_CHECK_LIB(rt, clock_gettime, 
           [
              AC_DEFINE(HAVE_CLOCK_GETTIME,1,"Whether or not clock_gettime can be found in system libraries")
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
-I\$(top_srcdir) -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS -Wall"

JACK_LIBC_HELPER_FLAGS=
AC_ARG_ENABLE(ancient_libc,
	AC_HELP_STRING([--enable-ancient-libc],[Add required CFLAGS for libc versions too old to expose PThread R/W Lock and other modern code]),
	JACK_LIBC_HELPER_FLAGS="-D_XOPEN_SOURCE=600 -D_BSD_SOURCE",
	JACK_LIBC_HELPER_FLAGS=""
	)
	 
JACK_CORE_CFLAGS="$JACK_CORE_CFLAGS $JACK_LIBC_HELPER_FLAGS"   

AC_ARG_WITH(cpu-target,
            [  --with-cpu-target=cpu-type    explicit, overriding argument for gcc -march= flag])


dnl
dnl figure out how best to optimize
dnl JOQ: this should be done via config/configure.hosts
dnl

dnl ----------------------------------------------------------------------

if test "$target_cpu" = "powerpc64" ; then

    AC_ARG_ENABLE(cell,
	AC_HELP_STRING([--enable-cell],[enable Cell BE support (default=no)]),,
	[ enable_cell=no ])

    if test "x$enable_cell" = xyes; then
        AC_DEFINE(ENABLE_CELLBE, 1, [Define to 1 if you need support for the Cell BE.])
    fi

fi

if test "$target_cpu" = "powerpc" -o "$target_cpu" = "powerpc64" ; then

    AC_DEFINE(POWERPC, 1, "Are we running a ppc CPU?")
    
############################
# Check for Altivec assembly
############################

    AC_ARG_ENABLE(altivec,
	AC_HELP_STRING([--enable-altivec],[enable Altivec support (default=auto)]),,
	[ enable_altivec=yes ])
    
    if test "x$enable_altivec" = xyes; then
	
	AC_MSG_CHECKING(whether we can compile Altivec code)
	
	AC_COMPILE_IFELSE([asm ("vand %v0, %v0, %v0");],
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

    if test "$target_cpu" = "powerpc64"; then
	JACK_OPT_CFLAGS="-O2 -mcpu=powerpc64 $ALTIVEC_OPT_FLAGS -mhard-float" 
    fi

elif echo $target_cpu | egrep '(i.86|x86_64)' >/dev/null; then

    dnl Check for MMX/SSE assembly
    
    if test -r /proc/cpuinfo ; then
	procflags=`grep '^flags' /proc/cpuinfo`
	if echo $procflags | grep -s mmx ; then
	    cpu_supports_mmx=yes
	fi
	if echo $procflags | grep -s sse ; then
	    cpu_supports_sse=yes
	fi
    else
	# this is not linux, but assume that if the processor
	# is x86 then is supports SSE and not MMX
        cpu_supports_sse=yes
	AC_MSG_WARN([Assuming your x86/x86_64 system can support SSE. Use --disable-sse if this is not the case])
        cpu_supports_mmx=no
	AC_MSG_WARN([Assuming your x86/x86_64 system does not need to use MMX. Use --enable-mmx if this is not the case])
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
    
    AC_ARG_ENABLE(mmx,
	AC_HELP_STRING([--enable-mmx],[enable MMX support (default=auto)]),,
	enable_mmx=yes)
    
    AC_ARG_ENABLE(sse,
	AC_HELP_STRING([--enable-sse],[enable SSE support (default=auto)]),,
	enable_sse=$enable_mmx)
    
    if test "x$enable_mmx" = xyes; then
	
	AC_MSG_CHECKING(whether we can compile MMX code)
	
	AC_COMPILE_IFELSE([asm ("movq 0, %mm0");],
	    
	    AC_DEFINE(USE_MMX, 1, [Define to 1 if MMX assembly is available.])
	    AC_MSG_RESULT(yes)
	    
	    if test x$optimization_by_cpu = xyes ; then
		if test x$cpu_supports_mmx = xyes ; then
		    MMX_FLAGS="-mmmx"
		fi
	    else 
		MMX_FLAGS="-mmmx"
	    fi
	    
	    AC_MSG_RESULT(yes)
	    ,
	    enable_mmx=no
	    AC_MSG_RESULT(no)
	    AC_MSG_WARN([The assembler does not support the MMX command set.])
	)
    fi

    if test "x$enable_sse" = xyes; then
	
	AC_MSG_CHECKING(whether we can compile SSE code)
	
	AC_COMPILE_IFELSE([asm ("movntps %xmm0, 0");],
	    [
		if test x$optimization_by_cpu = xyes ; then
		    if test x$cpu_supports_sse = xyes ; then
			SSE_FLAGS="-msse -mfpmath=sse"
			MMX_FLAGS=""
		    fi
		else 
		    SSE_FLAGS="-msse -mfpmath=sse"
		    MMX_FLAGS=""
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
dnl the target_cpu a little more precisely. on os x we always get "i386"
dnl as the CPU type. we miss out on some possible optimizations as 
dnl a result. oh well.
dnl     
    
    if test x$with_cpu_target != x ; then
        JACK_OPT_CFLAGS="-march=$with_cpu_target"
    else
        if test "$target_cpu" = "i586"; then
	   JACK_OPT_CFLAGS="-march=i586 "
	elif test "$target_cpu" = "i686"; then
	    JACK_OPT_CFLAGS="-march=i686"
	elif test "$target_cpu" = "x86_64"; then
	    JACK_OPT_CFLAGS="-march=k8"
	else
	    :
	fi
    fi

dnl
dnl do not add h/w specific flags if asked to let compiler
dnl to the optimization
dnl    

    if test x$optimization_by_compiler != xyes ; then
        JACK_OPT_CFLAGS="$COMMON_X86_OPT_FLAGS $JACK_OPT_CFLAGS $MMX_FLAGS $SSE_FLAGS"
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


AC_ARG_ENABLE(ensure-mlock,
	AC_HELP_STRING([--enable-ensure-mlock], [server should fail if unable to lock memory (default=no)]),
        [ 
	  if test x$enable_ensure_mlock != xno ; then
	  	AC_DEFINE(ENSURE_MLOCK,,[Ensure that memory locking succeeds])
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
	CFLAGS="$CFLAGS $SAMPLERATE_CFLAGS"
	LIBS="$LIBS $SAMPLERATE_LIBS"
    	AC_DEFINE(HAVE_SAMPLERATE,1,"Whether libsamplerate is available")
fi

# Celt low-latency audio codec. netjack transmission via internet.
HAVE_CELT=false
PKG_CHECK_MODULES(CELT, celt >= 0.5.0,[HAVE_CELT=true], [true])
if test x$HAVE_CELT = xfalse; then
    	AC_DEFINE(HAVE_CELT,0,"Whether CELT is available")
	AC_MSG_RESULT(no)
        AC_MSG_WARN([*** NetJack will not be built with celt support])
else
    	AC_DEFINE(HAVE_CELT,1,"Whether CELT is available")
	AC_MSG_RESULT(yes)
	LIBS="$LIBS $CELT_LIBS"
fi

# Note: A bug in pkg-config causes problems if the first occurence of
# PKG_CHECK_MODULES can be disabled. So, if you're going to use
# PKG_CHECK_MODULES inside a --disable-whatever check, you need to
# do it somewhere *below* this comment.

# Check which backend drivers can be built.  The last one successfully
# configured becomes the default JACK driver; so the order of
# precedence is: alsa, sun, oss, coreaudio, portaudio, dummy.

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
if test "x$TRY_OSS" = "xyes"
then
	# check for Open Sound System
	AC_CHECK_HEADER([sys/soundcard.h],
	     [HAVE_OSS="true"
              JACK_DEFAULT_DRIVER=\"oss\"])
fi
AM_CONDITIONAL(HAVE_OSS, $HAVE_OSS)

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
	# check for ALSA >= 1.0.0
	PKG_CHECK_MODULES(ALSA, alsa >= 1.0.0,
	     [HAVE_ALSA="true"
              ALSA_LIBS=-lasound
              JACK_DEFAULT_DRIVER=\"alsa\"
             ], AC_MSG_RESULT([no]), [-lm]
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

AC_OUTPUT(
Makefile
config/Makefile
config/cpu/Makefile
config/cpu/alpha/Makefile
config/cpu/cris/Makefile
config/cpu/generic/Makefile
config/cpu/i386/Makefile
config/cpu/i486/Makefile
config/cpu/ia64/Makefile
config/cpu/m68k/Makefile
config/cpu/mips/Makefile
config/cpu/powerpc/Makefile
config/cpu/s390/Makefile
config/os/Makefile
config/os/generic/Makefile
config/os/gnu-linux/Makefile
config/os/macosx/Makefile
config/sysdeps/Makefile
doc/Makefile
doc/reference.doxygen
drivers/Makefile
drivers/alsa/Makefile
drivers/alsa-midi/Makefile
drivers/dummy/Makefile
drivers/oss/Makefile
drivers/sun/Makefile
drivers/portaudio/Makefile
drivers/coreaudio/Makefile
drivers/freebob/Makefile
drivers/firewire/Makefile
drivers/netjack/Makefile
example-clients/Makefile
tools/Makefile
man/Makefile
jack.pc
jack.spec
jack/Makefile
jack/version.h
jackd/Makefile
jackd/jackd.1
libjack/Makefile
)

dnl
dnl Output summary message
dnl

echo
echo $PACKAGE $VERSION :
echo
echo \| Build with ALSA support............................... : $HAVE_ALSA
echo \| Build with old FireWire \(FreeBob\) support............. : $HAVE_FREEBOB
echo \| Build with new FireWire \(FFADO\) support............... : $HAVE_FIREWIRE
echo \| Build with OSS support................................ : $HAVE_OSS
echo \| Build with Sun audio support.......................... : $HAVE_SUN
echo \| Build with CoreAudio support.......................... : $HAVE_COREAUDIO
echo \| Build with PortAudio support.......................... : $HAVE_PA
echo \| Build with Celt support............................... : $HAVE_CELT
echo \| Build with dynamic buffer size support................ : $buffer_resizing
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

