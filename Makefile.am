## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = foreign dist-zip

SUBDIRS = src conf doc web win32

EXTRA_DIST = README AUTHORS COPYING

# SCCS Definitions (for BitKeeper)
GET = true

debug:
	$(MAKE) all CFLAGS="@DEBUG@ @XML_CFLAGS@ @OGG_CFLAGS@ @VORBIS_CFLAGS@"

profile:
	$(MAKE) all CFLAGS="@PROFILE@ @XML_CFLAGS@ @OGG_CFLAGS@ @VORBIS_CFLAGS@"
