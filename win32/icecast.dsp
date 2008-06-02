# Microsoft Developer Studio Project File - Name="icecast" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Static Library" 0x0104

CFG=icecast - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "icecast.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "icecast.mak" CFG="icecast - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "icecast - Win32 Release" (based on "Win32 (x86) Static Library")
!MESSAGE "icecast - Win32 Debug" (based on "Win32 (x86) Static Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "icecast - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "releaselib_tmp"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /YX /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /I "../../curl/include" /I "..\src" /I "../" /I "../../libxslt/include" /I "../../iconv/include" /I "../../libxml2/include" /I "../../pthreads" /I "../../oggvorbis-win32sdk-1.0.1/include" /I "../../theora/include" /I "../../speex/include" /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /D "HAVE_CURL" /D "USE_YP" /D "HAVE_SYS_STAT_H" /D PACKAGE_VERSION=\"2.3.2\" /D "HAVE_LOCALTIME_R" /D "HAVE_OLD_VSNPRINTF" /D "HAVE_THEORA" /D "HAVE_SPEEX" /D "HAVE_AUTH_URL" /D sock_t=SOCKET /D "HAVE_WINSOCK2_H" /YX /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "debuglib_tmp"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /YX /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "../../curl/include" /I "..\src" /I "../" /I "../../libxslt/include" /I "../../iconv/include" /I "../../libxml2/include" /I "../../pthreads" /I "../../oggvorbis-win32sdk-1.0.1/include" /I "../../theora/include" /I "../../speex/include" /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /D "_WIN32" /D "HAVE_CURL" /D "USE_YP" /D "HAVE_SYS_STAT_H" /D PACKAGE_VERSION=\"2.3.2\" /D "HAVE_LOCALTIME_R" /D "HAVE_OLD_VSNPRINTF" /D "HAVE_THEORA" /D "HAVE_SPEEX" /D "HAVE_AUTH_URL" /FD /D /GZ /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ENDIF 

# Begin Target

# Name "icecast - Win32 Release"
# Name "icecast - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\src\admin.c
# End Source File
# Begin Source File

SOURCE=..\src\auth.c
# End Source File
# Begin Source File

SOURCE=..\src\auth_htpasswd.c
# End Source File
# Begin Source File

SOURCE=..\src\auth_url.c
# End Source File
# Begin Source File

SOURCE=..\src\avl\avl.c
# End Source File
# Begin Source File

SOURCE=..\src\cfgfile.c
# End Source File
# Begin Source File

SOURCE=..\src\client.c
# End Source File
# Begin Source File

SOURCE=..\src\connection.c
# End Source File
# Begin Source File

SOURCE=..\src\event.c
# End Source File
# Begin Source File

SOURCE=..\src\format.c
# End Source File
# Begin Source File

SOURCE=..\src\format_flac.c
# End Source File
# Begin Source File

SOURCE=..\src\format_kate.c
# End Source File
# Begin Source File

SOURCE=..\src\format_midi.c
# End Source File
# Begin Source File

SOURCE=..\src\format_mp3.c
# End Source File
# Begin Source File

SOURCE=..\src\format_ogg.c
# End Source File
# Begin Source File

SOURCE=..\src\format_skeleton.c
# End Source File
# Begin Source File

SOURCE=..\src\format_speex.c
# End Source File
# Begin Source File

SOURCE=..\src\format_theora.c
# End Source File
# Begin Source File

SOURCE=..\src\format_vorbis.c
# End Source File
# Begin Source File

SOURCE=..\src\fserve.c
# End Source File
# Begin Source File

SOURCE=..\src\global.c
# End Source File
# Begin Source File

SOURCE=..\src\httpp\httpp.c
# End Source File
# Begin Source File

SOURCE=..\src\log\log.c
# End Source File
# Begin Source File

SOURCE=..\src\logging.c
# End Source File
# Begin Source File

SOURCE=..\src\md5.c
# End Source File
# Begin Source File

SOURCE=..\src\os.h
# End Source File
# Begin Source File

SOURCE=..\src\refbuf.c
# End Source File
# Begin Source File

SOURCE=..\src\net\resolver.c
# End Source File
# Begin Source File

SOURCE=..\src\sighandler.c
# End Source File
# Begin Source File

SOURCE=..\src\slave.c
# End Source File
# Begin Source File

SOURCE=..\src\net\sock.c
# End Source File
# Begin Source File

SOURCE=..\src\source.c
# End Source File
# Begin Source File

SOURCE=..\src\stats.c
# End Source File
# Begin Source File

SOURCE=..\src\thread\thread.c
# End Source File
# Begin Source File

SOURCE=..\src\timing\timing.c
# End Source File
# Begin Source File

SOURCE=..\src\util.c
# End Source File
# Begin Source File

SOURCE=..\src\xslt.c
# End Source File
# Begin Source File

SOURCE=..\src\yp.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\src\admin.h
# End Source File
# Begin Source File

SOURCE=..\src\auth.h
# End Source File
# Begin Source File

SOURCE=..\src\auth_htpasswd.h
# End Source File
# Begin Source File

SOURCE=..\src\auth_url.h
# End Source File
# Begin Source File

SOURCE=..\src\avl\avl.h
# End Source File
# Begin Source File

SOURCE=..\src\cfgfile.h
# End Source File
# Begin Source File

SOURCE=..\src\client.h
# End Source File
# Begin Source File

SOURCE=..\src\compat.h
# End Source File
# Begin Source File

SOURCE=..\src\connection.h
# End Source File
# Begin Source File

SOURCE=..\src\event.h
# End Source File
# Begin Source File

SOURCE=..\src\format.h
# End Source File
# Begin Source File

SOURCE=..\src\format_flac.h
# End Source File
# Begin Source File

SOURCE=..\src\format_kate.h
# End Source File
# Begin Source File

SOURCE=..\src\format_midi.h
# End Source File
# Begin Source File

SOURCE=..\src\format_mp3.h
# End Source File
# Begin Source File

SOURCE=..\src\format_ogg.h
# End Source File
# Begin Source File

SOURCE=..\src\format_skeleton.h
# End Source File
# Begin Source File

SOURCE=..\src\format_speex.h
# End Source File
# Begin Source File

SOURCE=..\src\format_theora.h
# End Source File
# Begin Source File

SOURCE=..\src\format_vorbis.h
# End Source File
# Begin Source File

SOURCE=..\src\fserve.h
# End Source File
# Begin Source File

SOURCE=..\src\global.h
# End Source File
# Begin Source File

SOURCE=..\src\httpp\httpp.h
# End Source File
# Begin Source File

SOURCE=..\src\log\log.h
# End Source File
# Begin Source File

SOURCE=..\src\logging.h
# End Source File
# Begin Source File

SOURCE=..\src\md5.h
# End Source File
# Begin Source File

SOURCE=..\src\refbuf.h
# End Source File
# Begin Source File

SOURCE=..\src\net\resolver.h
# End Source File
# Begin Source File

SOURCE=..\src\sighandler.h
# End Source File
# Begin Source File

SOURCE=..\src\net\sock.h
# End Source File
# Begin Source File

SOURCE=..\src\source.h
# End Source File
# Begin Source File

SOURCE=..\src\stats.h
# End Source File
# Begin Source File

SOURCE=..\src\thread\thread.h
# End Source File
# Begin Source File

SOURCE=..\src\timing\timing.h
# End Source File
# Begin Source File

SOURCE=..\src\util.h
# End Source File
# Begin Source File

SOURCE=..\src\xslt.h
# End Source File
# Begin Source File

SOURCE=..\src\yp.h
# End Source File
# End Group
# End Target
# End Project
