## Process this with automake to create Makefile.in

AUTOMAKE_OPTIONS = foreign

noinst_LTLIBRARIES = libicelog.la
noinst_HEADERS = log.h

libicelog_la_SOURCES = log.c

# SCCS stuff (for BitKeeper)
GET = true

debug:
        $(MAKE) all CFLAGS="@DEBUG@"

profile:
        $(MAKE) all CFLAGS="@PROFILE@"

