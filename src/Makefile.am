## Process this with automake to create Makefile.in

AUTOMAKE_OPTIONS = foreign

SUBDIRS = avl thread httpp net log timing

bin_PROGRAMS = icecast

noinst_HEADERS = config.h os.h logging.h sighandler.h connection.h global.h\
	 util.h source.h stats.h refbuf.h client.h format.h format_vorbis.h\
	 compat.h format_mp3.h
icecast_SOURCES = config.c main.c logging.c sighandler.c connection.c global.c\
	 util.c source.c stats.c refbuf.c client.c format.c format_vorbis.c\
	 format_mp3.c
    
icecast_LDADD = net/libicenet.la thread/libicethread.la httpp/libicehttpp.la\
		log/libicelog.la avl/libiceavl.la timing/libicetiming.la

LIBS = @LIBS@ -lpthread @SOCKET_LIBS@ @XML_LIBS@ @OGG_LIBS@ @VORBIS_LIBS@
CFLAGS = @CFLAGS@ @XML_CFLAGS@ @OGG_CFLAGS@ @VORBIS_CFLAGS@

INCLUDES = -I$(srcdir)/net -I$(srcdir)/thread -I$(srcdir)/avl -I$(srcdir)/httpp \
	-I$(srcdir)/log -I$(srcdir)/timing

# SCCS stuff (for BitKeeper)
GET = true

debug:
        $(MAKE) all CFLAGS="@DEBUG@ @XML_CFLAGS@ @OGG_CFLAGS@ @VORBIS_CFLAGS@"

profile:
        $(MAKE) all CFLAGS="@PROFILE@ @XML_CFLAGS@ @OGG_CFLAGS@ @VORBIS_CFLAGS@"

