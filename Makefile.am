## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = foreign dist-zip

SUBDIRS = src conf doc web admin win32

EXTRA_DIST = HACKING

docdir = $(datadir)/doc/$(PACKAGE)
doc_DATA = README AUTHORS COPYING NEWS TODO

debug:
	$(MAKE) all CFLAGS="@DEBUG@ @XML_CFLAGS@ @OGG_CFLAGS@ @VORBIS_CFLAGS@"

profile:
	$(MAKE) all CFLAGS="@PROFILE@ @XML_CFLAGS@ @OGG_CFLAGS@ @VORBIS_CFLAGS@"
