# XIPH_NET
# Perform tests required by the net module
AC_DEFUN([XIPH_NET],
[dnl
AC_REQUIRE([XIPH_TYPE_SOCKLEN_T])
AC_REQUIRE([XIPH_FUNC_VA_COPY])
AC_CHECK_HEADERS([sys/select.h sys/uio.h])

# These tests are ordered based on solaris 8 tests
AC_SEARCH_LIBS([sethostent], [nsl],
  [AC_DEFINE([HAVE_SETHOSTENT], [1],
    [Define if you have the sethostent function])])
AC_SEARCH_LIBS([getnameinfo], [socket],
  [AC_DEFINE([HAVE_GETNAMEINFO], [1],
    [Define if you have the inet_pton function])])
AC_CHECK_FUNCS([endhostent getaddrinfo inet_aton inet_pton])
])
