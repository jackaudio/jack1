dnl AS_AC_EXPAND(VAR, CONFIGURE_VAR)
dnl example
dnl AS_AC_EXPAND(SYSCONFDIR, $sysconfdir)
dnl will set SYSCONFDIR to /usr/local/etc

dnl written by thomas vander stichele

AC_DEFUN(AS_AC_EXPAND,
[
  EXP_VAR=[$1]
  FROM_VAR=[$2]

  dnl first expand prefix and exec_prefix if necessary
  prefix_save=$prefix
  if test "x$prefix" = "xNONE"; then
    prefix=/usr/local
  fi
  exec_prefix_save=$exec_prefix
  if test "x$exec_prefix" = "xNONE"; then
    if test "x$prefix_save" = "xNONE"; then
      exec_prefix=/usr/local
    else
      exec_prefix=$prefix
    fi
  fi

  full_var="$FROM_VAR"
  dnl loop until it doesn't change anymore
  while true; do
    new_full_var="`eval echo $full_var`"
    if test "x$new_full_var"="x$full_var"; then break; fi
    full_var=$new_full_var
  done

  dnl clean up
  full_var=$new_full_var
  [$1]=$full_var
  prefix=$prefix_save
  exec_prefix=$exec_prefix_save
])

#
# fltk.m4
#
# You have permission to use this file under GNU's General Public License,
# version 2 or later
#
# Copyright (C) 2002 Robert Ham (node@users.sourceforge.net)
#

AC_DEFUN([AC_LIB_FLTK],[
  AC_LANG_PUSH([C++])
  AC_ARG_ENABLE(
    [fltktest],
    AC_HELP_STRING([--disable-fltktest],
                   [assume fltk is installed and skip the test]),
    [
      case "$enableval" in
        "yes")
          FLTK_TEST="yes"
          ;;
        "no")
          FLTK_TEST="no"
          ;;
        *)
          AC_MSG_ERROR([must use --enable-fltktest(=yes/no) or --disable-fltktest])
          ;;
      esac
    ],
    [ FLTK_TEST="yes"
    ])
                
  if test "$FLTK_TEST" = "no"; then
    AC_MSG_WARN([fltk test disabled: assuming FLTK_LDFLAGS="-lfltk" and FLTK_CXXFLAGS=""])
    FLTK_LDFLAGS="-lfltk"
    FLTK_CXXFLAGS=""
    FLTK_FOUND="yes"
  else
    AC_CACHE_VAL(
      [fltk_cv_prog_fltkconfig],
      [
        AC_ARG_WITH(
          [fltk-config],
          AC_HELP_STRING([--with-fltk-config=DIR],
                         [the directory containing the fltk-config program]),
          [
            case "$withval" in
              "yes")
                if test -d yes; then
                  FLTK_CONFIG_PATH="yes"
                else
                  AC_MSG_ERROR([you must use --with-fltk-config=DIR with DIR as a directory name])
                fi
                ;;
              "no")
                AC_MSG_ERROR([you must use --with-fltk-config=DIR with DIR as a directory name])
                ;;
              *)
                FLTK_CONFIG_PATH="$withval"
                ;;
            esac
          ])

        if test "$FLTK_CONFIG_PATH" = ""; then
          AC_PATH_PROG([FLTK_CONFIG], [fltk-config], [no])
        else
          AC_PATH_PROG([FLTK_CONFIG], [fltk-config], [no], "${FLTK_CONFIG_PATH}:${PATH}")
        fi

        if test "$FLTK_CONFIG" = "no"; then
          AC_MSG_WARN([could not find the fltk-config program - try using --with-fltk-config=DIR])
          FLTK_FOUND="no";
        fi

        fltk_cv_prog_fltkconfig="$FLTK_CONFIG"
      ])
    FLTK_CXXFLAGS="$( "$fltk_cv_prog_fltkconfig" --cxxflags )"
    FLTK_LDFLAGS="$( "$fltk_cv_prog_fltkconfig" --ldflags )"
    FLTK_FOUND="yes"
  fi
  AC_LANG_POP([C++])

  AC_SUBST(FLTK_CXXFLAGS)
  AC_SUBST(FLTK_LDFLAGS)
])
