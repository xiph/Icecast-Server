## Process this with automake to create Makefile.in

AUTOMAKE_OPTIONS = foreign

noinst_LTLIBRARIES = libicetiming.la
noinst_HEADERS = timing.h

libicetiming_la_SOURCES = timing.c
libicetiming_la_CFLAGS = @XIPH_CFLAGS@

# SCCS stuff (for BitKeeper)
GET = true

debug:
	$(MAKE) all CFLAGS="@DEBUG@"

profile:
	$(MAKE) all CFLAGS="@PROFILE@"

