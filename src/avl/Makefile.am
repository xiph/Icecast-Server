## Process this with automake to create Makefile.in

AUTOMAKE_OPTIONS = foreign

noinst_LTLIBRARIES = libiceavl.la
noinst_HEADERS = avl.h

libiceavl_la_SOURCES = avl.c
libiceavl_la_CFLAGS = @XIPH_CFLAGS@

INCLUDES = -I$(srcdir)/..

# SCCS stuff (for BitKeeper)
GET = true

debug:
	$(MAKE) all CFLAGS="@DEBUG@"

profile:
	$(MAKE) all CFLAGS="@PROFILE@"

