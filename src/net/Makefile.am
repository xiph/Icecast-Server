## Process this with automake to create Makefile.in

AUTOMAKE_OPTIONS = foreign

noinst_LTLIBRARIES = libicenet.la
noinst_HEADERS = resolver.h sock.h

libicenet_la_SOURCES = sock.c resolver.c

# SCCS stuff (for BitKeeper)
GET = true

debug:
        $(MAKE) all CFLAGS="@DEBUG@"

profile:
        $(MAKE) all CFLAGS="@PROFILE@"

