## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src conf doc web admin win32 examples tests

EXTRA_DIST = README.md HACKING tap-driver.sh

docdir = $(datadir)/doc/$(PACKAGE)
doc_DATA = README.md AUTHORS COPYING NEWS ChangeLog

dist_pkgdata_DATA = report-db.xml

static:
	$(MAKE) all LDFLAGS="${LDFLAGS} -all-static"
