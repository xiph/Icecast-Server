## Process this with automake to create Makefile.in

AUTOMAKE_OPTIONS = foreign

noinst_LTLIBRARIES = libicenet.la
noinst_HEADERS = resolver.h sock.h

libicenet_la_SOURCES = sock.c resolver.c
libicenet_la_CFLAGS = @XIPH_CFLAGS@

INCLUDES = -I$(srcdir)/..

debug:
	$(MAKE) all CFLAGS="@DEBUG@"

profile:
	$(MAKE) all CFLAGS="@PROFILE@

