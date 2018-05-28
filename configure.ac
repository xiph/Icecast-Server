AC_INIT([Icecast], [2.4.99.2], [icecast@xiph.org])

AC_PREREQ([2.54])

AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])

AC_PROG_CC
AC_PROG_CC_C99

AS_IF([test "${ac_cv_prog_cc_c99}" = "no"], [
  AC_MSG_ERROR([No C99 compiler found!])
])

AC_SYS_LARGEFILE

dnl Set build/host to default values
AC_CANONICAL_HOST
AC_CANONICAL_BUILD


AM_INIT_AUTOMAKE([tar-ustar foreign dist-zip])

AM_SILENT_RULES([yes])
AM_MAINTAINER_MODE([enable])

LT_INIT

dnl Checks for header files.
AC_HEADER_ASSERT
AC_HEADER_STDC
AC_HEADER_TIME

AC_CHECK_HEADERS([stdint.h inttypes.h], [
  ice_found_int_headers="yes";
  break;
])

AS_IF([test "$ice_found_int_headers" != "yes"], [
  AC_MSG_ERROR([Unable to find the standard integers headers])
])

AC_CHECK_HEADERS([sys/types.h])
AC_CHECK_HEADERS([sys/timeb.h])
AC_CHECK_HEADERS([sys/socket.h])
AC_CHECK_HEADERS([pwd.h grp.h])

XIPH_NET

dnl Check for functions
AC_FUNC_FORK
AC_FUNC_CHOWN

AC_CHECK_FUNCS([setuid])
AC_CHECK_FUNCS([chroot])
AC_CHECK_FUNCS([strcasestr])
AC_CHECK_FUNCS([gethostname])
AC_CHECK_FUNCS([uname])
AC_CHECK_FUNCS([setenv])
AC_CHECK_FUNCS([setresuid])
AC_CHECK_FUNCS([setresgid])
AC_CHECK_FUNCS([localtime_r])
AC_CHECK_FUNCS([poll])
AC_CHECK_FUNCS([gettimeofday])
AC_CHECK_FUNCS([ftime])

AC_SEARCH_LIBS([nanosleep], [rt posix4], [
  AC_DEFINE([HAVE_NANOSLEEP], [1], [Define if you have nanosleep])
])

dnl Checks for types and typedefs
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UID_T

dnl Checks for required libraries

dnl
dnl libxml2
dnl
PKG_CHECK_MODULES([LIBXML2], [libxml-2.0], [], [
  AC_MSG_ERROR([${LIBXML2_PKG_ERRORS}. libxml2 is required.])
])

CFLAGS="${CFLAGS} ${LIBXML2_CFLAGS}"
LIBS="${LIBS} ${LIBXML2_LIBS}"


dnl
dnl libxslt
dnl
PKG_CHECK_MODULES([LIBXSLT], [libxslt], [], [
  AC_MSG_ERROR([${LIBXSLT_PKG_ERRORS}. libxslt is required.])
])

CFLAGS="${CFLAGS} ${LIBXSLT_CFLAGS}"
LIBS="${LIBS} ${LIBXSLT_LIBS}"


dnl
dnl libvorbis
dnl
PKG_CHECK_MODULES([VORBIS], [vorbis >= 1.0], [], [
  AC_MSG_ERROR([${VORBIS_PKG_ERRORS}. Must have libvorbis v1.0 or above installed.])
])

CFLAGS="${CFLAGS} ${VORBIS_CFLAGS}"
LIBS="${LIBS} ${VORBIS_LIBS}"


dnl Checks for optional libraries

dnl
dnl libogg
dnl
PKG_HAVE_WITH_MODULES([OGG], [ogg], [
  CFLAGS="${CFLAGS} ${OGG_CFLAGS}"
  LIBS="${LIBS} ${OGG_LIBS}"
])

dnl
dnl libtheora
dnl
PKG_HAVE_WITH_MODULES([THEORA], [theora], [
  CFLAGS="${CFLAGS} ${THEORA_CFLAGS}"
  LIBS="${LIBS} ${THEORA_LIBS}"
])

dnl
dnl libspeex
dnl
PKG_HAVE_WITH_MODULES([SPEEX], [speex], [
  CFLAGS="${CFLAGS} ${SPEEX_CFLAGS}"
  LIBS="${LIBS} ${SPEEX_LIBS}"
])

dnl
dnl libkate
dnl
PKG_HAVE_WITH_MODULES([KATE], [kate], [
  CFLAGS="${CFLAGS} ${KATE_CFLAGS}"
  LIBS="${LIBS} ${KATE_LIBS}"
])

dnl
dnl libcurl
dnl
PKG_HAVE_WITH_MODULES([CURL], [libcurl], [
  CFLAGS="${CFLAGS} ${CURL_CFLAGS}"
  LIBS="${LIBS} ${CURL_LIBS}"
])

dnl
dnl openssl
dnl
PKG_HAVE_WITH_MODULES([OPENSSL], [openssl], [
  CFLAGS="${CFLAGS} ${OPENSSL_CFLAGS}"
  LIBS="${LIBS} ${OPENSSL_LIBS}"
])

dnl Check for library-specific functions
AC_CHECK_FUNCS([xsltSaveResultToString])

dnl we still use format_kate as it doesn't need libkate to work
#ICECAST_OPTIONAL="$ICECAST_OPTIONAL format_kate.o"

ACX_PTHREAD([], [AC_MSG_ERROR([POSIX threads missing])])

CFLAGS="${CFLAGS} ${PTHREAD_CFLAGS}"
CPPFLAGS="${CPPFLAGS} ${PTHREAD_CPPFLAGS}"
LIBS="${LIBS} ${PTHREAD_LIBS}"

dnl Feature enable/disable arguments

AC_ARG_ENABLE([yp],
  AS_HELP_STRING([--enable-yp],
    [enable yellowpage directory listing support (default: auto)])
)

AS_IF([test "x$enable_yp" != "xno"], [
  AS_IF([test "x$have_curl" != "xyes"], [
    AS_IF([test "x$enable_yp" == "xyes"], [
      AC_MSG_ERROR([cURL is required for YP support])
    ])
    enable_yp="no"
  ], [
    AC_DEFINE([USE_YP], 1, [Define to compile in YP support code])
    enable_yp="yes"
  ])
])

AM_CONDITIONAL([ENABLE_YP],
               [test "x$enable_yp" = "xyes"])


AC_ARG_ENABLE([client-tests],
  AS_HELP_STRING([--enable-client-tests],
    [enable client tests module (default: disabled)])
)

AS_IF([test "x$enable_client_tests" == "xyes"], [
  AC_DEFINE([ENABLE_MODULE_CLIENT_TESTS], 1, [Define to compile client test module])
], [enable_client_tests="no"])

AM_CONDITIONAL([ENABLE_MODULE_CLIENT_TESTS],
               [test "x$enable_client_tests" = "xyes"])

dnl Make substitutions
AC_SUBST(XIPH_LIBS)
AC_SUBST(XIPH_CPPFLAGS)
AC_SUBST(XIPH_CFLAGS)
AC_SUBST(XIPH_LDFLAGS)

AC_CONFIG_HEADERS([config.h])

AC_CONFIG_FILES([
  Makefile
  conf/Makefile
  src/Makefile
  src/common/avl/Makefile
  src/common/httpp/Makefile
  src/common/thread/Makefile
  src/common/log/Makefile
  src/common/net/Makefile
  src/common/timing/Makefile
  doc/Makefile
  web/Makefile
  admin/Makefile
  admin/includes/Makefile
  win32/Makefile
  examples/Makefile
  tests/Makefile
])

AC_OUTPUT

echo "
Icecast configuration
---------------------
Version        : ${VERSION}
cURL           : ${have_curl}
TLS (openSSL)  : ${have_openssl}

Format/Codec support:
  Ogg          : ${have_ogg}
  Theora       : ${have_theora}
  Speex        : ${have_speex}
  Kate         : ${have_kate}

Features:
  YP support   : ${enable_yp}
  Client tests : ${enable_client_tests}"
