dnl local M4 configure macros
dnl Brendan Cully <brendan@xiph.org>
dnl $Id: xiph_path_python.m4,v 1.1 2003/06/13 19:22:16 brendan Exp $

# XIPH_PATH_PYTHON(PATH)
# Search for python in PATH, or in the path if none is given.
# Defines PYTHON_CPPFLAGS and PYTHON_LIBS if found
AC_DEFUN([XIPH_PATH_PYTHON],
  [
m4_pushdef([xpp_path], [$1])

PYTHON="no"

if test "xpp_path" != "yes"
then
  AC_MSG_CHECKING([python])
  if test -x "xpp_path"
  then
    PYTHON="xpp_path"
  fi
  AC_MSG_RESULT([$PYTHON])
else
  AC_PATH_PROGS([PYTHON], [python python2 python2.3 python2.2])
fi

m4_popdef([xpp_path])

if test "$PYTHON" != "no"
then
  # The library we're linking against
  PYTHON_LIB="_XIPH_PYTHON_CFG([$PYTHON], [LIBRARY])"
  
  # if LIBRARY is nonsensical, bail out
  if test $? -ne 0 -o -z "$PYTHON_LIB"
  then
    AC_MSG_WARN([Could not find library for $PYTHON])
    break
  fi
  # make library linker friendly. This is a hack, but I don't know what's better
  PYTHON_LIB=`echo "$PYTHON_LIB" | sed 's/lib//;s/\.a.*//;s/\.so.*//;s/\.dylib.*//'`

  # LDFLAGS
  PYTHON_LDFLAGS="-L[]_XIPH_PYTHON_CFG([$PYTHON], [LIBPL])"

  # Extra libraries required by python
  PYTHON_EXTRA_LIBS="_XIPH_PYTHON_CFG([$PYTHON], [LIBS])"
  PYTHON_EXTRA_LIBS="$PYTHON_EXTRA_LIBS _XIPH_PYTHON_CFG([$PYTHON], [SYSLIBS])"
  PYTHON_EXTRA_LIBS="$PYTHON_EXTRA_LIBS _XIPH_PYTHON_CFG([$PYTHON], [SHLIBS])"

  PYTHON_CPPFLAGS="-I[]_XIPH_PYTHON_CFG([$PYTHON], [INCLUDEPY])"

  # test header and library functionality
  saved_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $PYTHON_CPPFLAGS"
  AC_CHECK_HEADER([Python.h],
    [
    saved_LDFLAGS="$LDFLAGS"
    LDFLAGS="$LDFLAGS $PYTHON_LDFLAGS"
    AC_CHECK_LIB([$PYTHON_LIB], [Py_Initialize],
      [PYTHON_LIBS="-l$PYTHON_LIB $PYTHON_EXTRA_LIBS"],
      [AC_MSG_WARN([Could not link to the python library])],
      [$PYTHON_EXTRA_LIBS])
    LDFLAGS="$saved_LDFLAGS"
    ],
    [AC_MSG_WARN([Python.h doesn't appear to be usable])])
  CPPFLAGS="$saved_CPPFLAGS"
fi
  ])dnl

# _XIPH_PYTHON_CFG(PYTHONPATH, CFGVAR)
# Ask python in PYTHONPATH for the definition of CFGVAR
m4_define([_XIPH_PYTHON_CFG],
  [`$1 -c 'from distutils.sysconfig import get_config_var; print get_config_var("$2")' | sed 's/None//'`])
