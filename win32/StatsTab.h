#if !defined(AFX_STATSTAB_H__64B82CAB_8D6D_45A6_84FD_666F6317E5F2__INCLUDED_)
#define AFX_STATSTAB_H__64B82CAB_8D6D_45A6_84FD_666F6317E5F2__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// StatsTab.h : header file
//
#include "TabPageSSL.h"

/////////////////////////////////////////////////////////////////////////////
// CStatsTab dialog

class CStatsTab : public CTabPageSSL
{
// Construction
public:
	int m_colStats1Width;
	int m_colStats0Width;
	int m_colSource0Width;
	CStatsTab(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CStatsTab)
	enum { IDD = IDD_STATSDIALOG };
	CStatic	m_SLS;
	CListCtrl	m_StatsListCtrl;
	CListCtrl	m_SourceListCtrl;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CStatsTab)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CStatsTab)
	virtual BOOL OnInitDialog();
	afx_msg void OnDblclkSourcelist(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnRclickStatslist(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnClickSourcelist(NMHDR* pNMHDR, LRESULT* pResult);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_STATSTAB_H__64B82CAB_8D6D_45A6_84FD_666F6317E5F2__INCLUDED_)
