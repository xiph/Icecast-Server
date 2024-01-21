## Process this with automake to create Makefile.in

SUBDIRS = common/avl common/net common/thread common/httpp common/log common/timing

bin_PROGRAMS = icecast

noinst_HEADERS = \
    icecasttypes.h \
    version.h \
    admin.h \
    resourcematch.h \
    main.h \
    cfgfile.h \
    logging.h \
    sighandler.h \
    connection.h \
    connection_handle.h \
    global.h \
    util.h \
    util_string.h \
    util_crypt.h \
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
    prng.h \
    matchfile.h \
    tls.h \
    geoip.h \
    refobject.h \
    module.h \
    reportxml.h \
    reportxml_helper.h \
    json.h \
    xml2json.h \
    valuefile.h \
    string_renderer.h \
    listensocket.h \
    fastevent.h \
    navigation.h \
    event.h \
    ping.h \
    acl.h auth.h \
    metadata_xiph.h \
    format.h \
    format_ogg.h \
    format_mp3.h \
    format_ebml.h \
    format_text.h \
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
    version.c \
    cfgfile.c \
    logging.c \
    sighandler.c \
    connection.c \
    connection_handle.c \
    global.c \
    util.c \
    util_string.c \
    util_crypt.c \
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
    prng.c \
    matchfile.c \
    tls.c \
    geoip.c \
    refobject.c \
    module.c \
    reportxml.c \
    reportxml_helper.c \
    json.c \
    xml2json.c \
    valuefile.c \
    string_renderer.c \
    listensocket.c \
    fastevent.c \
    navigation.c \
    metadata_xiph.c \
    format.c \
    format_ogg.c \
    format_mp3.c \
    format_midi.c \
    format_flac.c \
    format_ebml.c \
    format_text.c \
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
    auth_static.c \
    auth_enforce_auth.c

if HAVE_CURL
icecast_SOURCES += \
    curl.c \
    ping.c \
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
    ping.c \
    yp.c \
    auth_url.c \
    event_url.c \
    format_vorbis.c \
    format_theora.c \
    format_speex.c

icecast_DEPENDENCIES = \
    common/net/libicenet.la \
    common/thread/libicethread.la \
    common/httpp/libicehttpp.la \
    common/log/libicelog.la \
    common/avl/libiceavl.la \
    common/timing/libicetiming.la

icecast_LDADD    = $(icecast_DEPENDENCIES)
icecast_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/common

include $(srcdir)/tests/Makefile.am
