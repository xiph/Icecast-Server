## Process this file with automake to produce Makefile.in

EXTRA_DIST = \
	mkdocs.yml \
	docs/img/icecast_shema.svg \
	docs/admin_interface.md \
	docs/auth.md \
	docs/basic_setup.md \
	docs/config_file.md \
	docs/index.md \
	docs/introduction.md \
	docs/relaying.md \
	docs/server_stats.md \
	docs/win32.md \
	docs/yp.md

docdir = $(datadir)/doc/icecast

# Documentation generation
if HAVE_MKDOCS

# Directory where the docs will end up
directory = $(top_srcdir)/doc/icecast/

EXTRA_DIST += $(directory)

mkdocs.stamp:
	cd "$(top_srcdir)/doc" && $(MKDOCS) build -d "$(directory)"
	echo STAMP > mkdocs.stamp

CLEANFILES = mkdocs.stamp

install-data-local:
	echo FIXMEFIXMEFIXME
	exit
	$(INSTALL) -d "$(directory)" "$(docdir)"

uninstall-local:
	( cd "$(directory)"; find . -type f -print0) | (cd "$(docdir)"; xargs -0 rm )

all-local: mkdocs.stamp

clean-local:
	rm -rf "$(directory)"

endif
