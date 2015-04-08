dnl XIPH_PATH_CURL([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for libcurl, and define CURL_CFLAGS and CURL_LIBS
dnl
dnl $Id$
dnl
AC_DEFUN([XIPH_PATH_CURL],
[dnl 
dnl Get the cflags and libraries
dnl
AC_ARG_WITH(curl,
    AC_HELP_STRING([--with-curl=PFX],[Prefix where libcurl is installed (optional)]),
    curl_prefix="$withval", curl_prefix="$CURL_PREFIX")

if test "x$curl_prefix" = "xno"
then
  AC_MSG_RESULT([libcurl support disabled by request])
else

AC_ARG_WITH(curl-config,
    AC_HELP_STRING([--with-curl-config=curl-config],[Use curl-config to find libcurl]),
    CURL_CONFIG="$withval", [AC_PATH_PROGS(CURL_CONFIG, [curl-config], "")])

if test "x$curl_prefix" != "x" -a "x$curl_prefix" != "xyes"; then
    CURL_LIBS="-L$curl_prefix/lib -lcurl"
    CURL_CFLAGS="-I$curl_prefix/include"
elif test "x$CURL_CONFIG" != "x"; then
    if ! test -x "$CURL_CONFIG"; then
        AC_MSG_ERROR([$CURL_CONFIG cannot be executed])
    fi
    CURL_LIBS="$($CURL_CONFIG --libs)"
    CURL_CFLAGS="$($CURL_CONFIG --cflags)"
else
    if test "x$prefix" = "xNONE"; then
        curl_prefix="/usr/local"
    else
        curl_prefix="$prefix"
    fi
    CURL_LIBS="-L$curl_prefix/lib -lcurl"
    CURL_CFLAGS="-I$curl_prefix/include"
fi

curl_ok="yes"

xt_curl_CPPFLAGS="$CPPFLAGS"
xt_curl_LIBS="$LIBS"
CPPFLAGS="$CPPFLAGS $CURL_CFLAGS"
LIBS="$CURL_LIBS $LIBS"
dnl
dnl Now check if the installed libcurl is sufficiently new.
dnl
AC_CHECK_HEADERS([curl/curl.h],, curl_ok="no") 
AC_MSG_CHECKING(for libcurl)
if test "$curl_ok" = "yes"
then
    AC_RUN_IFELSE(AC_LANG_SOURCE([
#include <curl/curl.h>
int main()
{
    return 0;
}
]),,[curl_ok="no"],[curl_ok="yes"])
fi
if test "$curl_ok" = "yes"; then
    AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_CURL, 1, [Define if you have libcurl.])
    ifelse([$1], , :, [$1])     
else
    AC_MSG_RESULT(no)
    CURL_LIBS=""
    CURL_CFLAGS=""
    ifelse([$2], , :, [$2])
fi
CPPFLAGS="$xt_curl_CPPFLAGS"
LIBS="$xt_curl_LIBS"
fi
AC_SUBST(CURL_CFLAGS)
AC_SUBST(CURL_LIBS)
])
