dnl xiph_compiler.m4
dnl $Id: xiph_compiler.m4,v 1.1 2003/06/24 00:58:10 brendan Exp $

dnl XIPH_CLEAN_CCFLAGS
dnl Brendan Cully <brendan@xiph.org> 20030612
dnl
# XIPH_CLEAN_CCFLAGS(flag-list, dest-shell-var-name)
# Filters out duplicate compiler flags
# Operates right-to-left on -l flags, left-to-right on everything else
# eg XIPH_CLEAN_CCFLAGS([-L/opt/lib -lfoo -lm -L/opt/lib -lbar -lm], [MY_LDFLAGS])
# => MY_LDFLAGS="-L/opt/lib -lfoo -lbar -lm"
# the cat<<EOF construct makes sure echo doesn't pick, say, -n
AC_DEFUN([XIPH_CLEAN_CCFLAGS],
[AC_REQUIRE([AC_PROG_FGREP])
xcc_REV_FLAGS=''
for flag in $1
do
  case "$flag" in
  -l*)
    xcc_REV_FLAGS="$flag $xcc_REV_FLAGS"
    ;;
  *)
    if { cat <<EOF
 $xcc_REV_FLAGS 
EOF
} | $FGREP -v -e " $flag " > /dev/null
    then
      xcc_REV_FLAGS="$flag $xcc_REV_FLAGS"
    fi
  esac
done

for flag in $xcc_REV_FLAGS
do
  if { cat <<EOF
 $2 
EOF
} | $FGREP -v -e " $flag " > /dev/null
  then
    $2="$flag $$2"
  fi
done
])dnl XIPH_CLEAN_CCFLAGS

dnl XIPH_FUNC_VA_COPY
dnl Karl Heyes
dnl
# XIPH_FUNC_VA_COPY
# Test for implementation of va_copy, or define appropriately if missing
AC_DEFUN([XIPH_FUNC_VA_COPY],
[dnl
AC_MSG_CHECKING([for va_copy])
AC_TRY_LINK([#include <stdarg.h>], [va_list ap1, ap2; va_copy(ap1, ap2);],
  AC_MSG_RESULT([va_copy]),
  [dnl
  AH_TEMPLATE([va_copy], [define if va_copy is not available])
  AC_TRY_LINK([#include <stdarg.h>], [va_list ap1, ap2; __va_copy(ap1, ap2);],
    [dnl
    AC_DEFINE([va_copy], [__va_copy])
    AC_MSG_RESULT([__va_copy])],
    [dnl
    AC_DEFINE([va_copy(dest,src)], [memcpy(&dest,&src,sizeof(va_list))])
    AC_MSG_RESULT([memcpy])
    ])
  ])
])
])dnl XIPH_FUNC_VA_COPY

dnl XIPH_C_ATTRIBUTE
dnl Karl Heyes
dnl
# XIPH_C_ATTRIBUTE
# Define __attribute__ to be empty if the compiler does not support it
AC_DEFUN([XIPH_C_ATTRIBUTE],
[dnl
AC_TRY_COMPILE([int func(void) __attribute__((unused));],
  [int x __attribute__ ((unused));],,[dnl
  AC_DEFINE([__attribute__(x)],, [Define to empty if __attribute__ is not supported])
])
])dnl XIPH_C_ATTRIBUTE

dnl XIPH_GCC_INCLUDE_WARNING
dnl Karl Heyes
dnl
# XIPH_GCC_INCLUDE_WARNING(include-dir, action-if-warning, action-if-not)
# Tests whether GCC emits a warning if explicitly asked to put include-dir
# in its include path, because GCC is already including that path by default
AC_DEFUN([XIPH_GCC_INCLUDE_WARNING],
[AC_REQUIRE([AC_PROG_CC])
xgiw_warning=no
if test x"$GCC" = "xyes"
then
  save_cflags="$CFLAGS"
  CFLAGS="-Werror -I$1"
  AC_TRY_COMPILE(,,,xgiw_warning=yes)
  CFLAGS="$save_cflags"
fi
if test "$xgiw_warning" = "yes"
then
  ifelse([$2],,:,[$2])
else
  ifelse([$3],,:,[$3])
fi
])dnl XIPH_GCC_INCLUDE_WARNING

dnl XIPH_VAR_APPEND
dnl Karl Heyes
dnl
# XIPH_VAR_APPEND(shell-var, list)
# Append each item in list to shell-var iff shell-var doesn't already have it
# eg XIPH_VAR_APPEND([CFLAGS], [-O2 -I/opt/packages/include])
AC_DEFUN([XIPH_VAR_APPEND],
[dnl
AC_REQUIRE([AC_PROG_FGREP])
for arg in $2
do
  if { cat <<EOF
 $$1 
EOF
} | $FGREP -v -e " $arg " > /dev/null
  then
    $1="$$1 $arg"
  fi
done
])dnl XIPH_VAR_APPEND

dnl XIPH_VAR_PREPEND
dnl Karl Heyes
dnl
# XIPH_VAR_PREPEND(shell-var, list)
# see XIPH_VAR_APPEND
AC_DEFUN([XIPH_VAR_PREPEND],
[dnl
AC_REQUIRE([AC_PROG_FGREP])
xvp_compare="$1"
filtered=""   
for arg in $2
do
  if { cat <<EOF
 $compare 
EOF
} | $FGREP -v -e " $arg " > /dev/null
  then
    compare="$arg $compare"
    filtered="$filtered $arg"
  fi
done
$1="$filtered $$1"
])dnl XIPH_VAR_PREPEND
