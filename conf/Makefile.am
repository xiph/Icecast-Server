## Process this with automake to create Makefile.in

AUTOMAKE_OPTIONS = foreign

EXTRA_DIST = icecast.xml.in

sysconf_DATA = icecast.xml

edit = sed -e 's,@pkgdatadir\@,$(pkgdatadir),g' \
	-e 's,@localstatedir\@,$(localstatedir),g' \
	-e 's,@PACKAGE\@,$(PACKAGE),g'

icecast.xml: $(srcdir)/icecast.xml.in
	$(edit) $(srcdir)/icecast.xml.in > icecast.xml

debug:
	$(MAKE) all CFLAGS="@DEBUG@"

profile:
	$(MAKE) all CFLAGS="@PROFILE@"

