// Icecast2win.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "Icecast2win.h"
#include "Icecast2winDlg.h"

extern "C" {
#include "xslt.h"
void initialize_subsystems(void);
void shutdown_subsystems(void);
}
#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CIcecast2winApp

BEGIN_MESSAGE_MAP(CIcecast2winApp, CWinApp)
	//{{AFX_MSG_MAP(CIcecast2winApp)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		//    DO NOT EDIT what you see in these blocks of generated code!
	//}}AFX_MSG
	ON_COMMAND(ID_HELP, CWinApp::OnHelp)
END_MESSAGE_MAP()

#include "colors.h"
/////////////////////////////////////////////////////////////////////////////
// CIcecast2winApp construction

CIcecast2winApp::CIcecast2winApp()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}

/////////////////////////////////////////////////////////////////////////////
// The one and only CIcecast2winApp object

CIcecast2winApp theApp;

/////////////////////////////////////////////////////////////////////////////
// CIcecast2winApp initialization

BOOL CIcecast2winApp::InitInstance()
{
	AfxEnableControlContainer();

	// Standard initialization
	// If you are not using these features and wish to reduce the size
	//  of your final executable, you should remove from the following
	//  the specific initialization routines you do not need.

    initialize_subsystems();
	if (strlen(m_lpCmdLine) > 0) {
		strcpy(m_configFile, m_lpCmdLine);
	}
	else {
		strcpy(m_configFile, ".\\icecast.xml");
	}



#ifdef _AFXDLL
	Enable3dControls();			// Call this when using MFC in a shared DLL
#else
	Enable3dControlsStatic();	// Call this when linking to MFC statically
#endif

	CIcecast2winDlg dlg;
	m_pMainWnd = &dlg;

//	SetDialogBkColor(BGCOLOR,TEXTCOLOR); 

	m_pIconList[0] = LoadIcon (MAKEINTRESOURCE(IDR_MAINFRAME));

	int nResponse = dlg.DoModal();
	if (nResponse == IDOK)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with OK
	}
	else if (nResponse == IDCANCEL)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with Cancel
	}

	// Since the dialog has been closed, return FALSE so that we exit the
	//  application, rather than start the application's message pump.
	return FALSE;
}

int CIcecast2winApp::ExitInstance()
{
    shutdown_subsystems();
    return CWinApp::ExitInstance();
}
