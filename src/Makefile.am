## Process this with automake to create Makefile.in

bin_PROGRAMS = icecast

noinst_HEADERS = \
    icecasttypes.h \
    admin.h \
    resourcematch.h \
    main.h \
    cfgfile.h \
    logging.h \
    sighandler.h \
    connection.h \
    global.h \
    util.h \
    errors.h \
    curl.h \
    slave.h \
    source.h \
    stats.h \
    refbuf.h \
    client.h \
    playlist.h \
    compat.h \
    fserve.h \
    xslt.h \
    yp.h \
    md5.h \
    matchfile.h \
    tls.h \
    refobject.h \
    buffer.h \
    module.h \
    reportxml.h \
    listensocket.h \
    fastevent.h \
    event.h \
    event_log.h \
    event_exec.h \
    event_url.h \
    acl.h auth.h \
    format.h \
    format_ogg.h \
    format_mp3.h \
    format_ebml.h \
    format_vorbis.h \
    format_theora.h \
    format_flac.h \
    format_speex.h \
    format_midi.h \
    format_kate.h \
    format_skeleton.h \
    format_opus.h

icecast_SOURCES = \
    main.c \
    cfgfile.c \
    logging.c \
    sighandler.c \
    connection.c \
    global.c \
    util.c \
    errors.c \
    slave.c \
    source.c \
    stats.c \
    refbuf.c \
    client.c \
    playlist.c \
    xslt.c \
    fserve.c \
    admin.c \
    resourcematch.c \
    md5.c \
    matchfile.c \
    tls.c \
    refobject.c \
    buffer.c \
    module.c \
    reportxml.c \
    listensocket.c \
    fastevent.c \
    format.c \
    format_ogg.c \
    format_mp3.c \
    format_midi.c \
    format_flac.c \
    format_ebml.c \
    format_kate.c \
    format_skeleton.c \
    format_opus.c \
    event.c \
    event_log.c \
    event_exec.c \
    acl.c \
    auth.c \
    auth_htpasswd.c \
    auth_anonymous.c \
    auth_static.c

if HAVE_CURL
icecast_SOURCES += \
    curl.c \
    auth_url.c \
    event_url.c
endif

if ENABLE_YP
icecast_SOURCES += yp.c
endif

if HAVE_OGG
icecast_SOURCES += format_vorbis.c
endif

if HAVE_THEORA
icecast_SOURCES += format_theora.c
endif

if HAVE_SPEEX
icecast_SOURCES += format_speex.c
endif

EXTRA_icecast_SOURCES = \
    curl.c \
    yp.c \
    auth_url.c \
    event_url.c \
    format_vorbis.c \
    format_theora.c \
    format_speex.c

include $(srcdir)/tests/Makefile.am
