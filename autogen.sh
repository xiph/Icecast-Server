#!/bin/sh
# Run this to set up the build system: configure, makefiles, etc.
# (based on the version in enlightenment's cvs)

package="icecast"

olddir=`pwd`
srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

cd "$srcdir"
DIE=0

(autoconf --version) < /dev/null > /dev/null 2>&1 || {
        echo
        echo "You must have autoconf installed to compile $package."
        echo "Download the appropriate package for your distribution,"
        echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/"
        DIE=1
}
VERSIONGREP="sed -e s/.*[^0-9\.]\([0-9]\.[0-9]\).*/\1/"
                                                                                
# do we need automake?
if test -r Makefile.am; then
  options=`fgrep AUTOMAKE_OPTIONS Makefile.am`
  AM_NEEDED=`echo "$options" | $VERSIONGREP`
  if test -z "$AM_NEEDED" || test "x$AM_NEEDED" = "x$options"; then
    echo -n "checking for automake..."
    AUTOMAKE=automake
    ACLOCAL=aclocal
    if ($AUTOMAKE --version < /dev/null > /dev/null 2>&1); then
      echo "yes"
    else
      echo "no"
      AUTOMAKE=""
    fi
  else
    echo -n "checking for automake $AM_NEEDED or later..."
    for am in automake-$AM_NEEDED automake$AM_NEEDED automake; do
      ($am --version < /dev/null > /dev/null 2>&1) || continue
      ver=`$am --version < /dev/null | head -1 | $VERSIONGREP`
      if test $ver = $AM_NEEDED; then
        AUTOMAKE=$am
        echo $AUTOMAKE
        break
      fi
    done
    test -z $AUTOMAKE &&  echo "no"
    echo -n "checking for aclocal $AM_NEEDED or later..."
    for ac in aclocal-$AM_NEEDED aclocal$AM_NEEDED aclocal; do
      ($ac --version < /dev/null > /dev/null 2>&1) || continue
      ver=`$ac --version < /dev/null | head -1 | $VERSIONGREP`
      if test $ver = $AM_NEEDED; then
        ACLOCAL=$ac
        echo $ACLOCAL
        break
      fi
    done
    test -z $ACLOCAL && echo "no"
  fi
  test -z $AUTOMAKE || test -z $ACLOCAL && {
        echo
        echo "You must have automake installed to compile $package."
        echo "Download the appropriate package for your distribution,"
        echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/"
        DIE=1
  }
fi

(libtoolize --version) < /dev/null > /dev/null 2>&1 || {
	echo
	echo "You must have libtool installed to compile $package."
	echo "Download the appropriate package for your system,"
	echo "or get the source from one of the GNU ftp sites"
	echo "listed in http://www.gnu.org/order/ftp.html"
	DIE=1
}

if test "$DIE" -eq 1; then
        exit 1
fi

echo "Generating configuration files for $package, please wait...."

ACLOCAL_FLAGS="$ACLOCAL_FLAGS -I m4"
if ! test -z $ACLOCAL; then
  echo "  $ACLOCAL $ACLOCAL_FLAGS"
  $ACLOCAL $ACLOCAL_FLAGS
fi

echo "  autoheader"
autoheader

echo "  libtoolize --automake"
libtoolize --automake

if ! test -z $AUTOMAKE; then
  echo "  $AUTOMAKE --add-missing"
  $AUTOMAKE --add-missing 
fi

echo "  autoconf"
autoconf

if test -z "$*"; then
        echo "I am going to run ./configure with no arguments - if you wish "
        echo "to pass any to it, please specify them on the $0 command line."
fi
cd $olddir
$srcdir/configure "$@" && echo
