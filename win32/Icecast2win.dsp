# Microsoft Developer Studio Project File - Name="Icecast2win" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Application" 0x0101

CFG=Icecast2win - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "Icecast2win.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "Icecast2win.mak" CFG="Icecast2win - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "Icecast2win - Win32 Release" (based on "Win32 (x86) Application")
!MESSAGE "Icecast2win - Win32 Debug" (based on "Win32 (x86) Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "Icecast2win - Win32 Release"

# PROP BASE Use_MFC 5
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 5
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "releasewin_tmp"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /Yu"stdafx.h" /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /I "../src" /I "../src/httpp" /I "../src/thread" /I "../src/log" /I "../src/avl" /I "../src/net" /I "src/timings" /I "../" /I "../../libxslt/include" /I "../../iconv/include" /I "../../libxml2/include" /I "../../pthreads" /I "../../oggvorbis-win32sdk-1.0.1/include" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D PACKAGE_VERSION=\"2.3.2\" /D "USE_YP" /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 /nologo /subsystem:windows /machine:I386
# ADD LINK32 libcurl.lib ogg_static_d.lib vorbis_static.lib libxml2.lib libxslt.lib iconv.lib pthreadVSE.lib ws2_32.lib winmm.lib theora_static_d.lib libspeex.lib /nologo /version:2.3 /subsystem:windows /machine:I386 /nodefaultlib:"libcd.lib" /nodefaultlib:"libcmtd.lib"
# SUBTRACT LINK32 /pdb:none

!ELSEIF  "$(CFG)" == "Icecast2win - Win32 Debug"

# PROP BASE Use_MFC 5
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 5
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "debugwin_tmp"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /Yu"stdafx.h" /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "../src" /I "../" /I "../../libxslt/include" /I "../../iconv/include" /I "../../libxml2/include" /I "../../pthreads" /I "../../oggvorbis-win32sdk-1.0.1/include" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_WIN32" /D PACKAGE_VERSION=\"2.3.2\" /D "USE_YP" /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept
# ADD LINK32 libcurl.lib ogg_static_d.lib vorbis_static_d.lib libxml2.lib libxslt.lib iconv.lib pthreadVSE.lib ws2_32.lib winmm.lib theora_static_d.lib libspeex.lib /nologo /subsystem:windows /incremental:no /debug /machine:I386 /nodefaultlib:"libcd.lib" /pdbtype:sept
# SUBTRACT LINK32 /pdb:none

!ENDIF 

# Begin Target

# Name "Icecast2win - Win32 Release"
# Name "Icecast2win - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=.\ConfigTab.cpp
# End Source File
# Begin Source File

SOURCE=.\Icecast2win.cpp
# End Source File
# Begin Source File

SOURCE=.\Icecast2win.rc
# End Source File
# Begin Source File

SOURCE=.\Icecast2winDlg.cpp
# End Source File
# Begin Source File

SOURCE=\xiph\icecast\trunk\icecast\src\main.c
# End Source File
# Begin Source File

SOURCE=.\StatsTab.cpp
# End Source File
# Begin Source File

SOURCE=.\Status.cpp
# End Source File
# Begin Source File

SOURCE=.\StdAfx.cpp
# ADD CPP /Yc"stdafx.h"
# End Source File
# Begin Source File

SOURCE=.\TabCtrlSSL.cpp
# End Source File
# Begin Source File

SOURCE=.\TabCtrlSSL.h
# End Source File
# Begin Source File

SOURCE=.\TabPageSSL.cpp
# End Source File
# Begin Source File

SOURCE=.\TabPageSSL.h
# End Source File
# Begin Source File

SOURCE=.\Traynot.cpp
# End Source File
# Begin Source File

SOURCE=.\TRAYNOT.H
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=.\colors.h
# End Source File
# Begin Source File

SOURCE=.\ConfigTab.h
# End Source File
# Begin Source File

SOURCE=.\Icecast2win.h
# End Source File
# Begin Source File

SOURCE=.\Icecast2winDlg.h
# End Source File
# Begin Source File

SOURCE=.\Resource.h
# End Source File
# Begin Source File

SOURCE=.\StatsTab.h
# End Source File
# Begin Source File

SOURCE=.\Status.h
# End Source File
# Begin Source File

SOURCE=.\StdAfx.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# Begin Source File

SOURCE=.\bitmap1.bmp
# End Source File
# Begin Source File

SOURCE=.\bitmap2.bmp
# End Source File
# Begin Source File

SOURCE=.\black.bmp
# End Source File
# Begin Source File

SOURCE=.\credits.bmp
# End Source File
# Begin Source File

SOURCE=.\cursor1.cur
# End Source File
# Begin Source File

SOURCE=.\cursor2.cur
# End Source File
# Begin Source File

SOURCE=.\green1.ico
# End Source File
# Begin Source File

SOURCE=.\icecast.ico
# End Source File
# Begin Source File

SOURCE=.\Icecast2.ico
# End Source File
# Begin Source File

SOURCE=.\res\Icecast2.ico
# End Source File
# Begin Source File

SOURCE=.\icecast2logo2.bmp
# End Source File
# Begin Source File

SOURCE=.\icecast2title.bmp
# End Source File
# Begin Source File

SOURCE=.\res\Icecast2win.ico
# End Source File
# Begin Source File

SOURCE=.\res\Icecast2win.rc2
# End Source File
# Begin Source File

SOURCE=.\ico00001.ico
# End Source File
# Begin Source File

SOURCE=.\icon1.ico
# End Source File
# Begin Source File

SOURCE=.\icon2.ico
# End Source File
# Begin Source File

SOURCE=.\running.bmp
# End Source File
# Begin Source File

SOURCE=.\stopped.bmp
# End Source File
# End Group
# Begin Source File

SOURCE=.\ReadMe.txt
# End Source File
# End Target
# End Project
