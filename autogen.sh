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
    echo Checking for automake version
    options=`fgrep AUTOMAKE_OPTIONS Makefile.am`
    AM_NEEDED=`echo "$options" | $VERSIONGREP`
    AM_PROGS=automake
    AC_PROGS=aclocal
    if test -n "$AM_NEEDED" && test "x$AM_NEEDED" != "x$options"
    then
        AM_PROGS="automake-$AM_NEEDED automake$AM_NEEDED $AM_PROGS"
        AC_PROGS="aclocal-$AM_NEEDED aclocal$AM_NEEDED $AC_PROGS"
    else
        AM_NEEDED=""
    fi
    AM_PROGS="$AUTOMAKE $AM_PROGS"
    AC_PROGS="$ACLOCAL $AC_PROGS"
    for am in $AM_PROGS; do
      ($am --version > /dev/null 2>&1) 2>/dev/null || continue
      ver=`$am --version | head -1 | $VERSIONGREP`
      AWK_RES=`echo $ver $AM_NEEDED | awk '{ if ( $1 >= $2 ) print "yes"; else print "no" }'`
      if test "$AWK_RES" = "yes"; then
        AUTOMAKE=$am
        echo "  found $AUTOMAKE"
        break
      fi
    done
    for ac in $AC_PROGS; do
      ($ac --version > /dev/null 2>&1) 2>/dev/null || continue
      ver=`$ac --version < /dev/null | head -1 | $VERSIONGREP`
      AWK_RES=`echo $ver $AM_NEEDED | awk '{ if ( $1 >= $2 ) print "yes"; else print "no" }'`
      if test "$AWK_RES" = "yes"; then
        ACLOCAL=$ac
        echo "  found $ACLOCAL"
        break
      fi
    done
    test -z $AUTOMAKE || test -z $ACLOCAL && {
        echo
        if test -n "$AM_NEEDED"; then
            echo "You must have automake version $AM_NEEDED installed"
            echo "to compile $package."
        else
            echo "You must have automake installed to compile $package."
        fi
        echo "Download the appropriate package for your distribution,"
        echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/"
        DIE=1
      }
fi

(libtoolize --version)  > /dev/null 2>&1 || {
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
if test -n "$ACLOCAL"; then
  echo "  $ACLOCAL $ACLOCAL_FLAGS"
  $ACLOCAL $ACLOCAL_FLAGS
fi

echo "  autoheader"
autoheader

echo "  libtoolize --automake"
libtoolize --automake

if test -n "$AUTOMAKE"; then
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
