; CLW file contains information for the MFC ClassWizard

[General Info]
Version=1
LastClass=CIcecast2winDlg
LastTemplate=CDialog
NewFileInclude1=#include "stdafx.h"
NewFileInclude2=#include "Icecast2win.h"

ClassCount=6
Class1=CIcecast2winApp
Class2=CIcecast2winDlg
Class3=CAboutDlg

ResourceCount=10
Resource1=IDD_ABOUTBOX
Resource2=IDR_MAINFRAME
Resource3=IDR_MENU3
Resource4=IDR_TRAY
Class4=CStatus
Resource5=IDR_MENU2
Class5=CConfigTab
Class6=CStatsTab
Resource6=IDD_SSTATUS
Resource7=IDD_CONFIGDIALOG
Resource8=IDD_ICECAST2WIN_DIALOG
Resource9=IDD_STATSDIALOG
Resource10=IDR_MENU4

[CLS:CIcecast2winApp]
Type=0
HeaderFile=Icecast2win.h
ImplementationFile=Icecast2win.cpp
Filter=N

[CLS:CIcecast2winDlg]
Type=0
HeaderFile=Icecast2winDlg.h
ImplementationFile=Icecast2winDlg.cpp
Filter=C
LastObject=ID_ABOUT_HELP
BaseClass=CResizableDialog
VirtualFilter=dWC

[CLS:CAboutDlg]
Type=0
HeaderFile=Icecast2winDlg.h
ImplementationFile=Icecast2winDlg.cpp
Filter=D

[DLG:IDD_ABOUTBOX]
Type=1
Class=CAboutDlg
ControlCount=3
Control1=IDC_STATIC,static,1342177283
Control2=IDC_STATIC,static,1342308480
Control3=IDOK,button,1342373889

[DLG:IDD_ICECAST2WIN_DIALOG]
Type=1
Class=CIcecast2winDlg
ControlCount=8
Control1=IDC_MAINTAB,SysTabControl32,1342177280
Control2=IDC_START,button,1342242816
Control3=IDC_AUTOSTART,button,1342251011
Control4=IDC_STATIC,static,1342177294
Control5=IDC_SERVERSTATUS,static,1342177294
Control6=IDC_STATIC_SS,static,1342308865
Control7=IDC_STATICBLACK,static,1342177294
Control8=IDC_HIDESYSTRAY,button,1342242816

[DLG:IDD_SSTATUS]
Type=1
Class=CStatus
ControlCount=5
Control1=IDC_FILLER2,static,1342308352
Control2=IDC_GLOBALSTAT_LIST,SysListView32,1350631425
Control3=IDC_STATIC_GS,static,1342308353
Control4=IDC_STATIC_RUN,static,1342308352
Control5=IDC_RUNNINGFOR,static,1342308352

[CLS:CStatus]
Type=0
HeaderFile=Status.h
ImplementationFile=Status.cpp
BaseClass=CTabPageSSL
Filter=D
LastObject=ID_POPUP_ADDTOGLOBALSTATLIST
VirtualFilter=dWC

[DLG:IDD_CONFIGDIALOG]
Type=1
Class=CConfigTab
ControlCount=1
Control1=IDC_CONFIG,edit,1352732868

[CLS:CConfigTab]
Type=0
HeaderFile=ConfigTab.h
ImplementationFile=ConfigTab.cpp
BaseClass=CTabPageSSL
Filter=D
VirtualFilter=dWC
LastObject=IDC_CONFIG

[DLG:IDD_STATSDIALOG]
Type=1
Class=CStatsTab
ControlCount=5
Control1=IDC_STATSLIST,SysListView32,1350631425
Control2=IDC_SOURCELIST,SysListView32,1350631425
Control3=IDC_FILLER1,static,1342308352
Control4=IDC_STATIC_SLS,static,1342308353
Control5=IDC_STATIC,static,1342308352

[CLS:CStatsTab]
Type=0
HeaderFile=StatsTab.h
ImplementationFile=StatsTab.cpp
BaseClass=CTabPageSSL
Filter=D
VirtualFilter=dWC
LastObject=IDC_SOURCELIST

[MNU:IDR_MENU2]
Type=1
Class=?
Command1=ID_POPUP_ADDTOGLOBALSTATLIST
CommandCount=1

[MNU:IDR_MENU3]
Type=1
Class=?
Command1=ID__DELETEFROMGLOBALSTATS
Command2=ID__MAKETHISSTATTHEWINDOWTITLE
CommandCount=2

[MNU:IDR_TRAY]
Type=1
Class=CIcecast2winDlg
Command1=ID_BLANK_RESTORE
CommandCount=1

[MNU:IDR_MENU4]
Type=1
Class=CIcecast2winDlg
Command1=ID_FILE_EXIT
Command2=ID_FILE_EDITCONFIGURATION
Command3=ID_ABOUT_HELP
Command4=ID_ABOUT_CREDITS
CommandCount=4

