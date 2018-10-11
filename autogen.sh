#!/bin/sh
# Run this to generate the configure script
set -e

srcdir=$(dirname "$0")
test -n "$srcdir" && cd "$srcdir"

# Test for any file in the m4 submodule
if ! test -e "$srcdir/m4/ax_check_compile_flag.m4"
then
    echo "ERROR: Submodule missing! Ensure you git cloned with '--recursive' or run:"
    echo "         git submodule update --init"
    exit 1
fi

echo "Updating build configuration files for Icecast, please wait..."

autoreconf -isf
