# Configure paths for libogg
# updated by Karl Heyes 10-Jun-2003
# Jack Moffitt <jack@icecast.org> 10-21-2000
# Shamelessly stolen from Owen Taylor and Manish Singh

dnl XIPH_PATH_OGG([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for libogg, and define OGG_CFLAGS OGG_LDFLAGS and OGG_LIBS
dnl
AC_DEFUN(XIPH_PATH_OGG,
[dnl 
AC_ARG_VAR([OGG_PREFIX],[path to ogg installation])
AC_ARG_WITH(ogg,
    [AC_HELP_STRING([--with-ogg=PREFIX],
                   [Prefix where libogg is installed (optional)])],
    ogg_prefix="$withval",
    ogg_prefix="$OGG_PREFIX"
)
if test "x$ogg_prefix" = "x"; then
    if test "x$prefix" = "xNONE"; then
        ogg_prefix=/usr/local
    else
        ogg_prefix="$prefix"
    fi
fi

XIPH_GCC_WARNING([-I$ogg_prefix/include],,
        [OGG_CFLAGS="-I$ogg_prefix/include"
        OGG_LDFLAGS="-L$ogg_prefix/lib"
        ])
OGG_LIBS="-logg"

#
# check if the installed Ogg is sufficiently new.
#
AC_MSG_CHECKING([for ogg_sync_init in libogg])
ac_save_CFLAGS="$CFLAGS"
ac_save_LIBS="$LIBS"
ac_save_LDFLAGS="$LDFLAGS"
CFLAGS="$CFLAGS $OGG_CFLAGS"
LIBS="$LIBS $OGG_LIBS"
LDFLAGS="$LDFLAGS $OGG_LDFLAGS"
AC_TRY_LINK_FUNC(ogg_sync_init,
        [ifelse([$1],, [AC_MSG_RESULT([ok])], [$1])],
        [AC_TRY_LINK([#include <ogg/ogg.h>],, 
            [ ifelse([$2], ,[AC_MSG_ERROR([found, but needs updating])], [$2])],
            [ ifelse([$2], ,[AC_MSG_ERROR([not found, maybe you need to set LD_LIBRARY_PATH or /etc/ld.so.conf])], [$2])])
        ])
CFLAGS="$ac_save_CFLAGS"
LDFLAGS="$ac_save_LDFLAGS"
LIBS="$ac_save_LIBS"

AC_SUBST(OGG_CFLAGS)
AC_SUBST(OGG_LDFLAGS)
AC_SUBST(OGG_LIBS)
])
