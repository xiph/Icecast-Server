# Configure paths for libvorbis
# Jack Moffitt <jack@icecast.org> 10-21-2000
# updated by Karl Heyes 31-Mar-2003
# Shamelessly stolen from Owen Taylor and Manish Singh

dnl XIPH_PATH_VORBIS([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for libvorbis, and define VORBIS_CFLAGS VORBIS_LIBS
dnl VORBIS_VORBISENC_LIBS VORBIS_VORBISFILE_LIBS VORBIS_LDFLAGS
dnl

AC_DEFUN([XIPH_PATH_VORBIS],
[
XIPH_PATH_OGG([$1],[$2])

dnl Get the cflags and libraries for vorbis
dnl
AC_ARG_VAR([VORBIS_PREFIX],[path to vorbis installation])
AC_ARG_WITH(vorbis,
    AC_HELP_STRING([--with-vorbis=PREFIX],
        [Prefix where libvorbis is installed (optional)]),
    vorbis_prefix="$withval",
    vorbis_prefix="$VORBIS_PREFIX"
    )
if test "x$vorbis_prefix" = "x"; then
    if test "x$prefix" = "xNONE"; then
        vorbis_prefix="/usr/local"
    else
        vorbis_prefix="$prefix"
    fi
fi

VORBIS_CFLAGS="$OGG_CFLAGS"
VORBIS_LDFLAGS="$OGG_LDFLAGS"
if test "x$vorbis_prefix" != "x$ogg_prefix"; then
    XIPH_GCC_INCLUDE_WARNING("$vorbis_prefix/include",,
            [VORBIS_CFLAGS="$VORBIS_CFLAGS -I$vorbis_prefix/include"
            VORBIS_LDFLAGS="-L$vorbis_prefix/lib $VORBIS_LDFLAGS"
            ])
fi

VORBIS_LIBS="-lvorbis"
VORBISFILE_LIBS="-lvorbisfile"
VORBISENC_LIBS="-lvorbisenc"

ac_save_LIBS="$LIBS"
ac_save_LDFLAGS="$LDFLAGS"
LDFLAGS="$LDFLAGS $VORBIS_LDFLAGS"
LIBS="$LIBS $VORBIS_LIBS"
AC_MSG_CHECKING([checking for libvorbis])
AC_TRY_LINK_FUNC(vorbis_info_init, [AC_MSG_RESULT([ok])],
        [LIBS="$LIBS $OGG_LIBS -lm"
        AC_TRY_LINK_FUNC(vorbis_info_init,
            [AC_MSG_RESULT([found, adding extra libs])
            VORBIS_LIBS="$VORBIS_LIBS $OGG_LIBS -lm"],
            [ifelse([$2], , AC_MSG_ERROR([Unable to link to libvorbis]), [$2])
            ])
        ])

LIBS="$ac_save_LIBS"
LDFLAGS="$ac_save_LDFLAGS"

#
# Now check if the installed Vorbis is sufficiently new.
#
ac_save_CFLAGS="$CFLAGS"
ac_save_LIBS="$LIBS"
CFLAGS="$CFLAGS $VORBIS_CFLAGS"
LIBS="$LIBS $VORBIS_LDFLAGS $VORBIS_LIBS"

AC_CHECK_TYPES([struct ovectl_ratemanage_arg],[vorbis_ok=yes],
        [ifelse([$2], ,[AC_MSG_ERROR([libvorbis needs updating])], [$2])], [
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>
        ])
CFLAGS="$ac_save_CFLAGS"
LIBS="$ac_save_LIBS"
AC_SUBST(VORBIS_CFLAGS)
AC_SUBST(VORBIS_LDFLAGS)
AC_SUBST(VORBIS_LIBS)
AC_SUBST(VORBISFILE_LIBS)
AC_SUBST(VORBISENC_LIBS)
])
