# Configure paths for libshout
# Jack Moffitt <jack@icecast.org> 08-06-2001
# Shamelessly stolen from Owen Taylor and Manish Singh

dnl AM_PATH_SHOUT([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for libshout, and define SHOUT_CFLAGS and SHOUT_LIBS
dnl
AC_DEFUN(AM_PATH_SHOUT,
[dnl 
dnl Get the cflags and libraries
dnl
AC_ARG_WITH(shout-prefix,[  --with-shout-prefix=PFX   Prefix where libshout is installed (optional)], shout_prefix="$withval", shout_prefix="")
AC_ARG_ENABLE(shouttest, [  --disable-shouttest       Do not try to compile and run a test Shout program],, enable_shouttest=yes)

  if test "x$shout_prefix" != "xNONE" ; then
    shout_args="$shout_args --prefix=$shout_prefix"
    SHOUT_CFLAGS="-I$shout_prefix/include"
    SHOUT_LIBS="-L$shout_prefix/lib"
  elif test "$prefix" != ""; then
    shout_args="$shout_args --prefix=$prefix"
    SHOUT_CFLAGS="-I$prefix/include"
    SHOUT_LIBS="-L$prefix/lib"
  fi

  SHOUT_LIBS="$SHOUT_LIBS -lshout"

  case $host in
  sparc-sun-*)
  	SHOUT_LIBS="$SHOUT_LIBS -lnsl -lsocket -lresolv"
  esac

  AC_MSG_CHECKING(for Shout)
  no_shout=""

  if test "x$enable_shouttest" = "xyes" ; then
    ac_save_CFLAGS="$CFLAGS"
    ac_save_LIBS="$LIBS"
    CFLAGS="$CFLAGS $SHOUT_CFLAGS $OGG_CFLAGS $VORBIS_CFLAGS"
    LIBS="$LIBS $SHOUT_LIBS $OGG_LIBS $VORBIS_LIBS"
dnl
dnl Now check if the installed Shout is sufficiently new.
dnl
      rm -f conf.shouttest
      AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shout/shout.h>

int main ()
{
  system("touch conf.shouttest");
  return 0;
}

],, no_shout=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
  fi

  if test "x$no_shout" = "x" ; then
     AC_MSG_RESULT(yes)
     ifelse([$1], , :, [$1])     
  else
     AC_MSG_RESULT(no)
     if test -f conf.shouttest ; then
       :
     else
       echo "*** Could not run Shout test program, checking why..."
       CFLAGS="$CFLAGS $SHOUT_CFLAGS $OGG_CFLAGS $VORBIS_CFLAGS"
       LIBS="$LIBS $SHOUT_LIBS $OGG_LIBS $VORBIS_LIBS"
       AC_TRY_LINK([
#include <stdio.h>
#include <shout/shout.h>
],     [ return 0; ],
       [ echo "*** The test program compiled, but did not run. This usually means"
       echo "*** that the run-time linker is not finding Shout or finding the wrong"
       echo "*** version of Shout. If it is not finding Shout, you'll need to set your"
       echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
       echo "*** to the installed location  Also, make sure you have run ldconfig if that"
       echo "*** is required on your system"
       echo "***"
       echo "*** If you have an old version installed, it is best to remove it, although"
       echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"],
       [ echo "*** The test program failed to compile or link. See the file config.log for the"
       echo "*** exact error that occured. This usually means Shout was incorrectly installed"
       echo "*** or that you have moved Shout since it was installed. In the latter case, you"
       echo "*** may want to edit the shout-config script: $SHOUT_CONFIG" ])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
     fi
     SHOUT_CFLAGS=""
     SHOUT_LIBS=""
     ifelse([$2], , :, [$2])
  fi
  AC_SUBST(SHOUT_CFLAGS)
  AC_SUBST(SHOUT_LIBS)
  rm -f conf.shouttest
])
