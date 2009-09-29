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
# PROP Output_Dir "lib_release"
# PROP Intermediate_Dir "lib_release"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /YX /FD /c
# ADD CPP /nologo /MT /W3 /GX /Zd /O2 /I ".." /I "..\src" /D "NDEBUG" /D "_LIB" /D HAVE_CONFIG_H=1 /D "WIN32" /D "_MBCS" /YX /FD /c
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
# PROP Output_Dir "lib_debug"
# PROP Intermediate_Dir "lib_debug"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /YX /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I ".." /I "..\src" /D "_DEBUG" /D "_LIB" /D "WIN32" /D "_MBCS" /D HAVE_CONFIG_H=1 /YX /FD /GZ /c
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

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\auth.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\auth_htpasswd.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\auth_url.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\avl\avl.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\cfgfile.c

!IF  "$(CFG)" == "icecast - Win32 Release"

# ADD CPP /I "."

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\client.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\connection.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\event.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\fnmatch.c

!IF  "$(CFG)" == "icecast - Win32 Release"

# ADD CPP /I "."

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\format.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\format_flac.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\format_kate.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\format_midi.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\format_mp3.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\format_ogg.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\format_skeleton.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\format_speex.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\format_theora.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\format_vorbis.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\fserve.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\global.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\httpp\httpp.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\log\log.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\logging.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\md5.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\refbuf.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\net\resolver.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\sighandler.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\slave.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\net\sock.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\source.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\stats.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\thread\thread.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\timing\timing.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\util.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\xslt.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\src\yp.c

!IF  "$(CFG)" == "icecast - Win32 Release"

!ELSEIF  "$(CFG)" == "icecast - Win32 Debug"

!ENDIF 

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

SOURCE=\config.h
# End Source File
# Begin Source File

SOURCE=..\src\connection.h
# End Source File
# Begin Source File

SOURCE=..\src\event.h
# End Source File
# Begin Source File

SOURCE=.\fnmatch.h
# End Source File
# Begin Source File

SOURCE=..\src\format.h
# End Source File
# Begin Source File

SOURCE=..\src\format_flac.h
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

SOURCE=..\src\slave.h
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
