#if !defined(AFX_CONFIGTAB_H__D8B0CC28_59FA_44E8_91A4_377C64F67DCF__INCLUDED_)
#define AFX_CONFIGTAB_H__D8B0CC28_59FA_44E8_91A4_377C64F67DCF__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// ConfigTab.h : header file
//
#include "TabPageSSL.h"

/////////////////////////////////////////////////////////////////////////////
// CConfigTab dialog

class CConfigTab : public CTabPageSSL
{
// Construction
public:
	void SaveConfiguration();
	CConfigTab(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CConfigTab)
	enum { IDD = IDD_CONFIGDIALOG };
	CEdit	m_ConfigCtrl;
	CString	m_Config;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CConfigTab)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CConfigTab)
	virtual BOOL OnInitDialog();
	afx_msg void OnKillfocusConfig();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_CONFIGTAB_H__D8B0CC28_59FA_44E8_91A4_377C64F67DCF__INCLUDED_)
