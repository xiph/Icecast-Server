## Process this file with automake to produce Makefile.in

SUBDIRS = src conf doc web admin win32 examples tests

EXTRA_DIST = README.md HACKING

docdir = $(datadir)/doc/$(PACKAGE)
doc_DATA = README.md AUTHORS COPYING NEWS ChangeLog

debug:
	$(MAKE) all CFLAGS="@DEBUG@"

profile:
	$(MAKE) all CFLAGS="@PROFILE@"

static:
	$(MAKE) all LDFLAGS="${LDFLAGS} -all-static"
