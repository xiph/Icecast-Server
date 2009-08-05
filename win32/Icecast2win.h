// Icecast2win.h : main header file for the ICECAST2WIN application
//

#if !defined(AFX_ICECAST2WIN_H__76A528C9_A424_4417_BFDF_0E556A9EE4F1__INCLUDED_)
#define AFX_ICECAST2WIN_H__76A528C9_A424_4417_BFDF_0E556A9EE4F1__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __AFXWIN_H__	
#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"		// main symbols

/////////////////////////////////////////////////////////////////////////////
// CIcecast2winApp:
// See Icecast2win.cpp for the implementation of this class
//

class CIcecast2winApp : public CWinApp
{
public:
	char m_configFile[1024];
	HICON m_pIconList[2];
	CIcecast2winApp();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CIcecast2winApp)
	public:
	virtual BOOL InitInstance();
    virtual int ExitInstance();
	//}}AFX_VIRTUAL

// Implementation

	//{{AFX_MSG(CIcecast2winApp)
		// NOTE - the ClassWizard will add and remove member functions here.
		//    DO NOT EDIT what you see in these blocks of generated code !
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

extern CIcecast2winApp theApp;
#endif // !defined(AFX_ICECAST2WIN_H__76A528C9_A424_4417_BFDF_0E556A9EE4F1__INCLUDED_)
