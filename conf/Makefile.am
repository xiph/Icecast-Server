## Process this with automake to create Makefile.in

AUTOMAKE_OPTIONS = foreign

EXTRA_DIST = icecast.xml.in
DISTCLEANFILES = icecast.xml.dist

docdir = $(datadir)/$(PACKAGE)/doc
doc_DATA = icecast.xml.dist

install-data-hook:
	$(mkinstalldirs) $(DESTDIR)$(sysconfdir)
	test -f $(DESTDIR)$(sysconfdir)/icecast.xml || \
	$(INSTALL_DATA) icecast.xml.dist $(DESTDIR)$(sysconfdir)/icecast.xml

edit = sed -e 's,@pkgdatadir\@,$(pkgdatadir),g' \
	-e 's,@localstatedir\@,$(localstatedir),g' \
	-e 's,@PACKAGE\@,$(PACKAGE),g'

icecast.xml.dist: $(srcdir)/icecast.xml.in
	$(edit) $(srcdir)/icecast.xml.in > icecast.xml.dist

debug:
	$(MAKE) all CFLAGS="@DEBUG@"

profile:
	$(MAKE) all CFLAGS="@PROFILE@"

