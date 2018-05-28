## Process this file with automake to produce Makefile.in

SUBDIRS = src conf doc web admin win32 examples tests

EXTRA_DIST = README.md HACKING m4/acx_pthread.m4 m4/ogg.m4 \
    m4/theora.m4 m4/vorbis.m4 m4/speex.m4 \
    m4/xiph_compiler.m4 m4/xiph_curl.m4 m4/xiph_net.m4 \
    m4/xiph_types.m4 m4/xiph_xml2.m4

docdir = $(datadir)/doc/$(PACKAGE)
doc_DATA = README.md AUTHORS COPYING NEWS ChangeLog

debug:
	$(MAKE) all CFLAGS="@DEBUG@"

profile:
	$(MAKE) all CFLAGS="@PROFILE@"

static:
	$(MAKE) all LDFLAGS="${LDFLAGS} -all-static"
