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
    if test "x$new_full_var" = "x$full_var"; then break; fi
    full_var=$new_full_var
  done

  dnl clean up
  full_var=$new_full_var
  [$1]=$full_var
  prefix=$prefix_save
  exec_prefix=$exec_prefix_save
])

