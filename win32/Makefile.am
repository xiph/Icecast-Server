## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = foreign

SUBDIRS = res

EXTRA_DIST = ConfigTab.cpp ConfigTab.h Icecast2win.clw Icecast2win.cpp \
    Icecast2win.dsp Icecast2win.dsw Icecast2win.h Icecast2win.rc \
    Icecast2winDlg.cpp Icecast2winDlg.h Makefile.am \
    StatsTab.cpp StatsTab.h Status.cpp Status.h StdAfx.cpp \
    StdAfx.h TabCtrlSSL.cpp TabCtrlSSL.h TabPageSSL.cpp TabPageSSL.h black.bmp \
    colors.h icecast.dsp icecast.ico icecast2.iss icecast2logo2.bmp\
    resource.h running.bmp stopped.bmp TRAYNOT.h Traynot.cpp \
    icecast2_console.dsw icecast2_console.dsp credits.bmp icecast2title.bmp \
    icecastService.cpp icecastService.dsp

bin_PROGRAMS = icecast
icecast_DEPENDENCIES = ../src/libicecast.a ../src/net/libicenet.la \
    ../src/thread/libicethread.la ../src/httpp/libicehttpp.la ../src/log/libicelog.la \
    ../src/avl/libiceavl.la ../src/timing/libicetiming.la

icecast_SOURCES = icecastService.cpp
icecast_CPPFLAGS = -I../src @XIPH_CFLAGS@ @XIPH_CPPFLAGS@
icecast_LDADD = $(icecast_DEPENDENCIES) @XIPH_LDFLAGS@ @XIPH_LIBS@ @KATE_LIBS@

