#if !defined(AFX_RESIZABLEDIALOG_H__INCLUDED_)
#define AFX_RESIZABLEDIALOG_H__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

// ResizableDialog.h : header file
//
/////////////////////////////////////////////////////////////////////////////
//
// Copyright (C) 2000 by Paolo Messina
// (ppescher@yahoo.com)
//
// Free for non-commercial use.
// You may change the code to your needs,
// provided that credits to the original 
// author is given in the modified files.
//  
/////////////////////////////////////////////////////////////////////////////

#include <afxtempl.h>
#include <afxwin.h>

// useful compatibility constants (the only one required is NOANCHOR)

#if !defined(__SIZE_ANCHORS_)
#define __SIZE_ANCHORS_

const CSize
	NOANCHOR(-1,-1),
	TOP_LEFT(0,0), TOP_CENTER(50,0), TOP_RIGHT(100,0),
	MIDDLE_LEFT(0,50), MIDDLE_CENTER(50,50), MIDDLE_RIGHT(100,50),
	BOTTOM_LEFT(0,100), BOTTOM_CENTER(50,100), BOTTOM_RIGHT(100,100);

#endif // !defined(__SIZE_ANCHORS_)

/////////////////////////////////////////////////////////////////////////////
// CResizableDialog window

class CResizableDialog : public CDialog
{

// Construction
public:
	CResizableDialog();
	CResizableDialog(UINT nIDTemplate, CWnd* pParentWnd = NULL);
	CResizableDialog(LPCTSTR lpszTemplateName, CWnd* pParentWnd = NULL);

// Attributes
private:
	// flags
	BOOL m_bShowGrip;
	BOOL m_bUseMaxTrack;
	BOOL m_bUseMinTrack;
	BOOL m_bUseMaxRect;
	BOOL m_bEnableSaveRestore;

	// internal status
	CString m_sSection;			// section name and
	CString m_sEntry;			// entry for save/restore

	BOOL m_bInitDone;			// if all internal vars initialized

	SIZE m_szGripSize;			// set at construction time

	CRect m_rcGripRect;			// current pos of grip

	POINT m_ptMinTrackSize;		// min tracking size
	POINT m_ptMaxTrackSize;		// max tracking size
	POINT m_ptMaxPos;			// maximized position
	POINT m_ptMaxSize;			// maximized size

	class Layout
	{
	public:
		HWND hwnd;

		BOOL adj_hscroll;
		BOOL need_refresh;

		// upper-left corner
		CSize tl_type;
		CSize tl_margin;
		
		// bottom-right corner
		CSize br_type;
		CSize br_margin;
	
	public:
		Layout()
			: hwnd(NULL), adj_hscroll(FALSE), need_refresh(FALSE),
			tl_type(0,0), tl_margin(0,0),
			br_type(0,0), br_margin(0,0)
		{
		};

		Layout(HWND hw, SIZE tl_t, SIZE tl_m, 
			SIZE br_t, SIZE br_m, BOOL hscroll, BOOL refresh)
		{
			hwnd = hw;

			adj_hscroll = hscroll;
			need_refresh = refresh;

			tl_type = tl_t;
			tl_margin = tl_m;
			
			br_type = br_t;
			br_margin = br_m;
		};
	};

	CArray<Layout, Layout&> m_arrLayout;	// list of repositionable controls

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CResizableDialog)
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~CResizableDialog();

// used internally
private:
	void Construct();
	void LoadWindowRect();
	void SaveWindowRect();
	void ArrangeLayout();
	void UpdateGripPos();

// callable from derived classes
//protected:
public:
	void AddAnchor(HWND wnd, CSize tl_type,
		CSize br_type = NOANCHOR);	// add anchors to a control
	void AddAnchor(UINT ctrl_ID, CSize tl_type,
		CSize br_type = NOANCHOR)	// add anchors to a control
	{
		AddAnchor(::GetDlgItem(*this, ctrl_ID), tl_type, br_type);
	};
	void ShowSizeGrip(BOOL bShow);				// show or hide the size grip
	void SetMaximizedRect(const CRect& rc);		// set window rect when maximized
	void ResetMaximizedRect();					// reset to default maximized rect
	void SetMinTrackSize(const CSize& size);	// set minimum tracking size
	void ResetMinTrackSize();					// reset to default minimum tracking size
	void SetMaxTrackSize(const CSize& size);	// set maximum tracking size
	void ResetMaxTrackSize();					// reset to default maximum tracking size
	void EnableSaveRestore(LPCTSTR pszSection, LPCTSTR pszEntry);	// section and entry in app's profile

// Generated message map functions
protected:
	//{{AFX_MSG(CResizableDialog)
	virtual BOOL OnInitDialog();
	afx_msg UINT OnNcHitTest(CPoint point);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO FAR* lpMMI);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
	afx_msg void OnPaint();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_RESIZABLEDIALOG_H__INCLUDED_)