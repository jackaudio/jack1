dnl Process this file with autoconf to produce a configure script.
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
JACK_MINOR_VERSION=99
JACK_MICRO_VERSION=30

dnl ---
dnl HOWTO: updating the jack protocol version
dnl
dnl increment the protocol version whenever a change is
dnl made to the way libjack communicates with jackd
dnl that would break applications linked with an older
dnl version of libjack.
dnl ---
JACK_PROTOCOL_VERSION=14

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
JACK_API_REVISION=23
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

AM_CONFIG_HEADER(config.h)
AC_ENABLE_STATIC(no)
AC_ENABLE_SHARED(yes)
AC_PROG_CC
AC_PROG_CXX
AC_PROG_LD
AM_PROG_LIBTOOL
AC_PROG_LN_S
AC_C_BIGENDIAN

# configure CPU and OS dependencies for host platform
AC_MSG_CHECKING([platform dependencies])
source $srcdir/config/configure.host
AC_MSG_RESULT([${os_include_dir}, ${cpu_include_dir}])
AC_SUBST(OS_LDFLAGS)
AC_SUBST(os_include_dir)
AC_SUBST(cpu_include_dir)

# system-dependent config.h values
test "x$JACK_THREAD_STACK_TOUCH" = "x" && JACK_THREAD_STACK_TOUCH=1000000
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
AC_CHECK_HEADERS(string.h strings.h)
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

# should we use mlockall() on this platform?
if test "x$JACK_DO_NOT_MLOCK" = "x"; then
    AC_CHECK_HEADER(sys/mman.h,
        [AC_CHECK_FUNC(mlockall,
	    [AC_DEFINE(USE_MLOCK, 1, [Use POSIX memory locking])])])
fi

# look for system support for POSIX shm API
AC_ARG_ENABLE(posix-shm,
	AC_HELP_STRING([--enable-posix-shm], [use POSIX shm API]),
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
	USE_POSIX_SHM="true"
else
	AC_MSG_RESULT([System V shmget().])
	JACK_SHM_REGISTRY_KEY=0x00282929
	AC_SUBST(JACK_SHM_REGISTRY_KEY)
	USE_POSIX_SHM="false"
fi
AM_CONDITIONAL(USE_POSIX_SHM, $USE_POSIX_SHM)

JACK_CORE_CFLAGS="-I\$(top_builddir)/config -I\$(top_srcdir) \
-I\$(top_builddir) -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS -Wall"
JACK_OPT_CFLAGS="$JACK_CORE_CFLAGS -g -march=pentium2 -mcpu=pentium4 -O3 \
-ffast-math -funroll-loops -fprefetch-loop-arrays"
JACK_CFLAGS="$JACK_CORE_CFLAGS $CFLAGS"

dnl
dnl figure out how best to optimize
dnl JOQ: this should be done via config/configure.hosts
dnl

dnl ----------------------------------------------------------------------

                                                                                
if test "$target_cpu" = "powerpc"; then

    AC_DEFINE(POWERPC, 1, "Are we running a ppc CPU?")
    
############################
# Check for Altivec assembly
############################

    AC_ARG_ENABLE(altivec,
	[  --enable-altivec        enable Altivec support (default=auto)],,
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
    
    JACK_OPT_CFLAGS="-D_REENTRANT -O2 -mcpu=7400 $ALTIVEC_OPT_FLAGS_FLAGS -mhard-float -mpowerpc-gfxopt"

elif echo $target_cpu | grep "i*86" >/dev/null; then

dnl Check for MMX assembly

AC_ARG_ENABLE(mmx,
    [  --enable-mmx            enable MMX support (default=auto)],,
    enable_mmx=yes)

AC_ARG_ENABLE(sse,
    [  --enable-sse            enable SSE support (default=auto)],,
    enable_sse=$enable_mmx)

if test "x$enable_mmx" = xyes; then
    
    AC_MSG_CHECKING(whether we can compile MMX code)
    
    AC_COMPILE_IFELSE([asm ("movq 0, %mm0");],
	
	AC_DEFINE(USE_MMX, 1, [Define to 1 if MMX assembly is available.])
	AC_MSG_RESULT(yes)
	MMX_FLAGS="-mmmx"
	
	if test "x$enable_sse" = xyes; then
	      
	      AC_MSG_CHECKING(whether we can compile SSE code)
	      
	      AC_COMPILE_IFELSE([asm ("movntps %xmm0, 0");],
		  AC_DEFINE(USE_SSE, 1,
                            [Define to 1 if SSE assembly is available.])
		  SSE_FLAGS="-msse -mfpmath=sse"
		  AC_MSG_RESULT(yes)
		  ,
		  enable_sse=no
		  AC_MSG_RESULT(no)
		  AC_MSG_WARN(
                      [The assembler does not support the SSE command set.]
                  )
	      )
	      
	  fi
	  ,
	  enable_mmx=no
	  AC_MSG_RESULT(no)
	  AC_MSG_WARN([The assembler does not support the MMX command set.])
      )
  fi

  AC_DEFINE(x86, 1, "Nope it's intel")
  COMMON_X86_OPT_FLAGS="-DREENTRANT -O3 -fomit-frame-pointer -ffast-math -funroll-loops -fmove-all-movables"

  if test "$target_cpu" = "i586"; then
      JACK_OPT_CFLAGS="-march=i586 "
  elif test "$target_cpu" = "i686"; then
      JACK_OPT_CFLAGS="-march=i686"
  else
      :
  fi

  JACK_OPT_CFLAGS="$COMMON_X86_OPT_FLAGS $JACK_OPT_CFLAGS $MMX_FLAGS $SSE_FLAGS"

fi

AC_ARG_ENABLE(optimize,
    AC_HELP_STRING([--disable-optimize], [do not ask the compiler to optimize]),
    [ if test x$enable_optimize != xno ; then
	JACK_CFLAGS="$JACK_CORE_CFLAGS $JACK_OPT_CFLAGS" ;
      fi 
    ]
)

AC_SUBST(JACK_CFLAGS)

dnl
dnl use JACK_CFLAGS for jackd compilation
dnl 

CFLAGS=$JACK_CFLAGS

# allow buffer resizing unless --disable-resize specified
enable_resize=yes
AC_ARG_ENABLE(resize,
	AC_HELP_STRING([--disable-resize], [disable buffer resizing]),
        [ 
	  if test x$enable_resize != xno ; then
	  	AC_DEFINE(DO_BUFFER_RESIZE,,[Enable buffer resizing])
	  fi
        ]
)		

AC_ARG_ENABLE(ensure-mlock,
	AC_HELP_STRING([--enable-ensure-mlock],
		[fail if unable to lock memory]),
        [ 
	  if test x$enable_ensure_mlock != xno ; then
	  	AC_DEFINE(ENSURE_MLOCK,,[Ensure that memory locking succeeds])
	  fi
	]
)

AC_ARG_ENABLE(debug,
	AC_HELP_STRING([--enable-debug],
		[enable debugging messages in jackd and libjack]),
        [ 
	  if test x$enable_debug != xno ; then
	     AC_DEFINE(DEBUG_ENABLED,,[Enable debugging messages])
          fi
        ]
)

AC_ARG_ENABLE(timestamps,
	AC_HELP_STRING([--enable-timestamps],
		[allow clients to use the JACK timestamp API]),
        [ 
	  if test x$enable_timestamps != xno ; then
	  	AC_DEFINE(WITH_TIMESTAMPS,,[Enable JACK timestamp API])
          fi
        ]		
)

AC_ARG_ENABLE(preemption-check,
	AC_HELP_STRING([--enable-preemption-check],
		[check for inappropriate realtime preemption]),
		[
		  if test x$enable_preemption_check != xno ; then
			echo checking for realtime preemption bugs
			AC_DEFINE(DO_PREEMPTION_CHECKING,,
				[check realtime preemption])
		  fi
		])

USE_CAPABILITIES=false

AC_ARG_ENABLE(capabilities,
              [  --enable-capabilities   use libcap to gain realtime scheduling priviledges],
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
              [  --disable-oldtrans      remove old transport interfaces],
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
              [  --enable-stripped-jackd strip jack before computing its md5 sum ],
              [ if test "x$USE_CAPABILITIES" != "xtrue" ; then
                    AC_MSG_WARN([*** capabilities not enabled, stripped jackd has no effect])
                elif test "x$enable_stripped_jackd" != "xno"; then
                    STRIPPED_JACKD=true
                fi
              ]
)

# plugins go in the addon dir.

AS_AC_EXPAND(ADDON_DIR,${libdir}/jack)
AC_SUBST(ADDON_DIR)
AC_DEFINE_UNQUOTED(ADDON_DIR,"$ADDON_DIR",[Directory for plugins])

AC_ARG_WITH(html-dir,
            [  --with-html-dir=PATH    where to install the html documentation])

if test "x$with_html_dir" = "x" ; then
    HTML_DIR='${pkgdatadir}'
else
    HTML_DIR=$with_html_dir
fi

AC_SUBST(HTML_DIR)


# allow specifying default tmpdir
AC_ARG_WITH(default-tmpdir,
            [  --with-default-tmpdir=PATH    where jackd and clients will put tmp files (/tmp)])

if test "x$with_default_tmpdir" = "x" ; then
	DEFAULT_TMP_DIR=/tmp
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
		       [avoid using pthread barrier functions]),
	[ USE_BARRIER=$withval ])
if test "x$USE_BARRIER" = "xyes"; then
	AC_CHECK_LIB([pthread], [pthread_barrier_init],
		AC_DEFINE(USE_BARRIER, 1, [Use pthread barrier functions]))
fi

# Check which drivers can be built.  The last one successfully
# configured becomes the default JACK driver; so the order of
# precedence is: alsa, oss, coreaudio, portaudio, dummy.

JACK_DEFAULT_DRIVER=\"dummy\"

AC_ARG_ENABLE(portaudio, [  --disable-portaudio     ignore PortAudio driver ],
			TRY_PORTAUDIO=$enableval , TRY_PORTAUDIO=yes ) 
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

AC_ARG_ENABLE(coreaudio, [  --disable-coreaudio     ignore CoreAudio driver ],
			TRY_COREAUDIO=$enableval , TRY_COREAUDIO=yes ) 
HAVE_COREAUDIO="false"
if test "x$TRY_COREAUDIO" = "xyes"
then
	# check for coreaudio
	AC_CHECK_HEADERS(CoreAudio/CoreAudio.h,
		[ HAVE_COREAUDIO="true"
		  CA_LIBS="-framework CoreAudio"
		  JACK_DEFAULT_DRIVER=\"coreaudio\"
		])
	AC_SUBST(CA_LIBS)
fi
AM_CONDITIONAL(HAVE_COREAUDIO, $HAVE_COREAUDIO)

AC_ARG_ENABLE(oss, [  --disable-oss           ignore OSS driver ],
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

AC_ARG_ENABLE(alsa, [  --disable-alsa          ignore ALSA driver ],
			TRY_ALSA=$enableval , TRY_ALSA=yes ) 
HAVE_ALSA="false"
if test "x$TRY_ALSA" = "xyes"
then
	# check for ALSA >= 0.9
	AC_CHECK_LIB(asound,snd_pcm_drop,
	     [HAVE_ALSA="true"
              ALSA_LIBS=-lasound
              JACK_DEFAULT_DRIVER=\"alsa\"
             ], [], [-lm]
	)
	AC_SUBST(ALSA_LIBS)
fi
AM_CONDITIONAL(HAVE_ALSA, $HAVE_ALSA)

if test "$JACK_DEFAULT_DRIVER" = \"dummy\"; then
  AC_MSG_WARN([Only the dummy driver can be built])
fi
AC_DEFINE_UNQUOTED(JACK_DEFAULT_DRIVER,
	[$JACK_DEFAULT_DRIVER],
	[Default JACK driver])

# some example-clients need libsndfile
HAVE_SNDFILE=false
PKG_CHECK_MODULES(SNDFILE, sndfile >= 1.0,[HAVE_SNDFILE=true], [true])
if test x$HAVE_SNDFILE = xfalse; then
             AC_MSG_WARN([*** the jackrec example client will not be built])
fi

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
AM_CONDITIONAL(HAVE_READLINE, $HAVE_READLINE)
AM_CONDITIONAL(HAVE_DOXYGEN, $HAVE_DOXYGEN)
AM_CONDITIONAL(USE_CAPABILITIES, $USE_CAPABILITIES)
AM_CONDITIONAL(STRIPPED_JACKD, $STRIPPED_JACKD)

AC_OUTPUT(
Makefile
config/Makefile
config/sysdeps/Makefile
doc/Makefile
doc/reference.doxygen
drivers/Makefile
drivers/alsa/Makefile
drivers/dummy/Makefile
drivers/oss/Makefile
drivers/portaudio/Makefile
drivers/coreaudio/Makefile
example-clients/Makefile
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
echo \| Build with OSS support................................ : $HAVE_OSS
echo \| Build with CoreAudio support.......................... : $HAVE_COREAUDIO
echo \| Build with PortAudio support.......................... : $HAVE_PA
echo \|
echo \| Default driver backend................................ : $JACK_DEFAULT_DRIVER
echo

