## Process this with automake to create Makefile.in

AUTOMAKE_OPTIONS = foreign

SUBDIRS = avl thread httpp net log timing

bin_PROGRAMS = icecast

noinst_HEADERS = config.h os.h logging.h sighandler.h connection.h global.h\
	 util.h source.h stats.h refbuf.h client.h format.h format_vorbis.h
icecast_SOURCES = config.c main.c logging.c sighandler.c connection.c global.c\
	 util.c source.c stats.c refbuf.c client.c format.c format_vorbis.c

icecast_LDADD = net/libicenet.la thread/libicethread.la httpp/libicehttpp.la\
		log/libicelog.la avl/libiceavl.la timing/libicetiming.la
LIBS = -lpthread

INCLUDES = -I$(srcdir)/net -I$(srcdir)/thread -I$(srcdir)/avl -I$(srcdir)/httpp \
	-I$(srcdir)/log -I$(srcdir)/timing

# SCCS stuff (for BitKeeper)
GET = true

debug:
        $(MAKE) all CFLAGS="@DEBUG@"

profile:
        $(MAKE) all CFLAGS="@PROFILE@"

