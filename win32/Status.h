#if !defined(AFX_STATUS_H__DE59E22B_FD4F_4131_B347_48BD9FAC9348__INCLUDED_)
#define AFX_STATUS_H__DE59E22B_FD4F_4131_B347_48BD9FAC9348__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// Status.h : header file
//
#include "TabPageSSL.h"

/////////////////////////////////////////////////////////////////////////////
// CStatus dialog

class CStatus : public CTabPageSSL
{
// Construction
public:
	int m_colStats2Width;
	int m_colStats1Width;
	int m_colStats0Width;
	CFont labelFont;
	CStatus(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	CBitmap runningBitmap;
	CBitmap stoppedBitmap;

	//{{AFX_DATA(CStatus)
	enum { IDD = IDD_SSTATUS };
	CStatic	m_GS;
	CListCtrl	m_GlobalStatList;
	CString	m_Clients;
	CString	m_Sources;
	CString	m_RunningFor;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CStatus)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CStatus)
	virtual BOOL OnInitDialog();
	afx_msg void OnRclickGlobalstatList(NMHDR* pNMHDR, LRESULT* pResult);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_STATUS_H__DE59E22B_FD4F_4131_B347_48BD9FAC9348__INCLUDED_)
