#!/bin/sh
# Run this to generate the configure script
set -e

srcdir=`dirname $0`
test -n "$srcdir" && cd "$srcdir"

echo "Updating build configuration files for Icecast, please wait..."

autoreconf -isf
