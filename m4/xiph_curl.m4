dnl XIPH_PATH_CURL([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for libcurl, and define CURL_CFLAGS and CURL_LIBS
dnl
AC_DEFUN(XIPH_PATH_CURL,
[dnl 
dnl Get the cflags and libraries
dnl
AC_ARG_WITH(curl,
    AC_HELP_STRING([--with-curl=PFX],[Prefix where libcurl is installed (optional)]),
    curl_prefix="$withval", curl_prefix="")
AC_ARG_WITH(curl-config,
    AC_HELP_STRING([--with-curl-config=curl-config],[Use curl-config to find libcurl]),
    CURL_CONFIG="$withval", [AC_PATH_PROGS(CURL_CONFIG, [curl-config], "")])

if test "x$curl_prefix" != "x"; then
    CURL_LIBS="-L$curl_prefix/lib -lcurl"
    CURL_CFLAGS="-I$curl_prefix/include"
elif test "x$CURL_CONFIG" != "x"; then
    if ! test -x "$CURL_CONFIG"; then
        AC_MSG_ERROR([$CURL_CONFIG cannot be executed])
    fi
    CURL_LIBS="$($CURL_CONFIG --libs)"
    CURL_CFLAGS="$($CURL_CONFIG --cflags)"
else
    if test "x$prefix" != "xNONE"; then
        CURL_LIBS="-L$prefix/lib"
        CURL_CFLAGS="-I$prefix/include"
    fi
    CURL_LIBS="$CURL_LIBS -lcurl"
fi

curl_ok="yes"

ac_curl_CPPFLAGS="$CPPFLAGS"
ac_curl_LIBS="$LIBS"
CPPFLAGS="$CPPFLAGS $CURL_CFLAGS"
LIBS="$CURL_LIBS $LIBS"
dnl
dnl Now check if the installed libcurl is sufficiently new.
dnl
AC_CHECK_HEADERS([curl/curl.h],, curl_ok="no") 
AC_MSG_CHECKING(for libcurl)
if test "$curl_ok" = "yes"; then
    AC_RUN_IFELSE([
#include <curl/curl.h>
int main()
{
    return 0;
}
],,[curl_ok="no"])
fi
CPPFLAGS="$ac_curl_CPPFLAGS"
LIBS="$ac_curl_LIBS"

if test "$curl_ok" = "yes"; then
    AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_CURL, 1, [Define if you have libcurl.])
    ifelse([$1], , :, [$1])     
else
    AC_MSG_RESULT(no)
    ifelse([$2], , :, [$2])
fi
])
