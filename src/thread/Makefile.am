## Process this with automake to create Makefile.in

AUTOMAKE_OPTIONS = foreign

noinst_LTLIBRARIES = libicethread.la
noinst_HEADERS = thread.h

libicethread_la_SOURCES = thread.c
libicethread_la_CFLAGS = @XIPH_CFLAGS@

INCLUDES = -I$(srcdir)/../avl -I$(srcdir)/../log

# SCCS stuff (for BitKeeper)
GET = true

debug:
	$(MAKE) all CFLAGS="@DEBUG@"

profile:
	$(MAKE) all CFLAGS="@PROFILE@"

