## Process this with automake to create Makefile.in

AUTOMAKE_OPTIONS = foreign

SUBDIRS = avl thread httpp net log timing

bin_PROGRAMS = icecast

noinst_HEADERS = admin.h cfgfile.h os.h logging.h sighandler.h connection.h \
    global.h util.h slave.h source.h stats.h refbuf.h client.h format.h \
    compat.h format_mp3.h fserve.h xslt.h yp.h event.h md5.h \
    auth.h auth_htpasswd.h auth_cmd.h auth_url.h format_ogg.h format_flac.h \
    format_vorbis.h format_theora.h format_speex.h format_midi.h
icecast_SOURCES = cfgfile.c main.c logging.c sighandler.c connection.c \
    global.c util.c slave.c source.c stats.c refbuf.c client.c \
    xslt.c fserve.c event.c admin.c md5.c \
    format.c format_ogg.c format_mp3.c format_midi.c format_flac.c \
    auth.c auth_htpasswd.c
EXTRA_icecast_SOURCES = yp.c auth_url.c auth_cmd.c \
    format_vorbis.c format_theora.c format_speex.c
    
icecast_DEPENDENCIES = @ICECAST_OPTIONAL@ net/libicenet.la thread/libicethread.la \
    httpp/libicehttpp.la log/libicelog.la avl/libiceavl.la timing/libicetiming.la
icecast_LDADD = $(icecast_DEPENDENCIES) @XIPH_LIBS@

AM_CFLAGS = @XIPH_CFLAGS@
AM_CPPFLAGS = @XIPH_CPPFLAGS@
AM_LDFLAGS = @XIPH_LDFLAGS@


debug:
	$(MAKE) all CFLAGS="@DEBUG@"

profile:
	$(MAKE) all CFLAGS="@PROFILE@"

