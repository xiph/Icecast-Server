dnl XIPH_PATH_SHOUT
dnl Jack Moffitt <jack@icecast.org> 08-06-2001
dnl Rewritten for libshout 2
dnl Brendan Cully <brendan@xiph.org> 20030612
dnl
# XIPH_PATH_SHOUT([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
# Test for libshout, and define SHOUT_CFLAGS and SHOUT_LIBS
AC_DEFUN([XIPH_PATH_SHOUT],
[dnl
have_shout="no"
SHOUT_CFLAGS=""
SHOUT_LIBS=""

# Step 1: Use pkg-config if available
m4_ifdef([PKG_CHECK_MODULES],
  [# PKG_CHECK_MODULES available
  PKG_CHECK_MODULES([SHOUT], [shout >= 2.0])
  have_shout="maybe"],
  [# PKG_CHECK_MODULES is unavailable, search for pkg-config program
  AC_PATH_PROG([PKGCONFIG], [pkg-config], [none])
  if test "$PKGCONFIG" != "none" && `$PKGCONFIG --exists 'shout >= 2.0'`
  then
    SHOUT_CFLAGS=`$PKGCONFIG --cflags`
    SHOUT_LIBS=`$PKGCONFIG --libs`
    have_shout="maybe"
  else
    # Step 2: try shout-config
    AC_PATH_PROG([SHOUTCONFIG], [shout-config], [none])
    if test "$SHOUTCONFIG" != "none" -a `$SHOUTCONFIG --package` = "libshout"
    then
      SHOUT_CFLAGS=`$SHOUTCONFIG --cflags`
      SHOUT_LIBS=`$SHOUTCONFIG --libs`
      have_shout="maybe"
    fi
  fi

  if test "$have_shout" != "no"
  then
    ac_save_CFLAGS="$CFLAGS"
    ac_save_LIBS="$LIBS"
    CFLAGS="$CFLAGS $SHOUT_CFLAGS"
    LIBS="$LIBS $SHOUT_LIBS"
    AC_CHECK_HEADER([shout/shout.h], [
      AC_DEFINE([HAVE_SHOUT_SHOUT_H], 1, [Define if you have <shout/shout.h>])
      AC_CHECK_FUNC([shout_new], [
        ifelse([$1], , :, [$1])
        have_shout="yes"
      ])
    ])
    CFLAGS="$ac_save_CFLAGS"
    LIBS="$ac_save_LIBS"
  fi

  if test "$have_shout" != "yes"
  then
    ifelse([$2], , :, [$2])
  fi
  ])
])dnl XIPH_PATH_SHOUT
