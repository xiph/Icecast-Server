// ResizableDialog.cpp : implementation file
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

#include "stdafx.h"
#include "ResizableDialog.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CResizableDialog

inline void CResizableDialog::Construct()
{
	m_bInitDone = FALSE;

	m_bUseMinTrack = TRUE;
	m_bUseMaxTrack = FALSE;
	m_bUseMaxRect = FALSE;

	m_bShowGrip = TRUE;
	
	m_bEnableSaveRestore = FALSE;

	m_szGripSize.cx = GetSystemMetrics(SM_CXVSCROLL);
	m_szGripSize.cy = GetSystemMetrics(SM_CYHSCROLL);
}

CResizableDialog::CResizableDialog()
{
	Construct();
}

CResizableDialog::CResizableDialog(UINT nIDTemplate, CWnd* pParentWnd)
	: CDialog(nIDTemplate, pParentWnd)
{
	Construct();
}

CResizableDialog::CResizableDialog(LPCTSTR lpszTemplateName, CWnd* pParentWnd)
	: CDialog(lpszTemplateName, pParentWnd)
{
	Construct();
}

CResizableDialog::~CResizableDialog()
{
	// for safety
	m_arrLayout.RemoveAll();
}


BEGIN_MESSAGE_MAP(CResizableDialog, CDialog)
	//{{AFX_MSG_MAP(CResizableDialog)
	ON_WM_NCHITTEST()
	ON_WM_GETMINMAXINFO()
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_PAINT()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


/////////////////////////////////////////////////////////////////////////////
// CResizableDialog message handlers


BOOL CResizableDialog::OnInitDialog() 
{
	CDialog::OnInitDialog();

	UpdateGripPos();

	// gets the template size as the min track size
	CRect rc;
	GetWindowRect(&rc);
	m_ptMinTrackSize.x = rc.Width();
	m_ptMinTrackSize.y = rc.Height();

	m_bInitDone = TRUE;

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CResizableDialog::OnDestroy() 
{
	CDialog::OnDestroy();
	
	if (m_bEnableSaveRestore)
		SaveWindowRect();

	// remove old windows
	m_arrLayout.RemoveAll();
}

void CResizableDialog::OnPaint() 
{
	CPaintDC dc(this); // device context for painting
	
	if (m_bShowGrip && !IsZoomed())
	{
		// draw size-grip
		dc.DrawFrameControl(&m_rcGripRect, DFC_SCROLL, DFCS_SCROLLSIZEGRIP);
	}
}

void CResizableDialog::OnSize(UINT nType, int cx, int cy) 
{
	CWnd::OnSize(nType, cx, cy);
	
	if (nType == SIZE_MAXHIDE || nType == SIZE_MAXSHOW)
		return;		// arrangement not needed

	if (m_bInitDone)
	{
		ArrangeLayout();
	}
}

UINT CResizableDialog::OnNcHitTest(CPoint point) 
{
	CPoint pt = point;
	ScreenToClient(&pt);

	// if in size grip and in client area
	if (m_bShowGrip && m_rcGripRect.PtInRect(pt) &&
		pt.x >= 0 && pt.y >= 0)
		return HTBOTTOMRIGHT;
	
	return CDialog::OnNcHitTest(point);
}

void CResizableDialog::OnGetMinMaxInfo(MINMAXINFO FAR* lpMMI) 
{
	if (!m_bInitDone)
		return;

	if (m_bUseMinTrack)
		lpMMI->ptMinTrackSize = m_ptMinTrackSize;

	if (m_bUseMaxTrack)
		lpMMI->ptMaxTrackSize = m_ptMaxTrackSize;

	if (m_bUseMaxRect)
	{
		lpMMI->ptMaxPosition = m_ptMaxPos;
		lpMMI->ptMaxSize = m_ptMaxSize;
	}
}

// layout functions

void CResizableDialog::AddAnchor(HWND wnd, CSize tl_type, CSize br_type)
{
	ASSERT(wnd != NULL && ::IsWindow(wnd));
	ASSERT(::IsChild(*this, wnd));
	ASSERT(tl_type != NOANCHOR);

	// get control's window class
	
	CString st;
	GetClassName(wnd, st.GetBufferSetLength(MAX_PATH), MAX_PATH);
	st.ReleaseBuffer();
	st.MakeUpper();

	// add the style 'clipsiblings' to a GroupBox
	// to avoid unnecessary repainting of controls inside
	if (st == "BUTTON")
	{
		DWORD style = GetWindowLong(wnd, GWL_STYLE);
		if (style & BS_GROUPBOX)
			SetWindowLong(wnd, GWL_STYLE, style | WS_CLIPSIBLINGS);
	}

	// wnd classes that don't redraw client area correctly
	// when the hor scroll pos changes due to a resizing
	BOOL hscroll = FALSE;
	if (st == "LISTBOX")
		hscroll = TRUE;

	// wnd classes that need refresh when resized
	BOOL refresh = FALSE;
	if (st == "STATIC")
	{
		DWORD style = GetWindowLong(wnd, GWL_STYLE);

		switch (style & SS_TYPEMASK)
		{
		case SS_LEFT:
		case SS_CENTER:
		case SS_RIGHT:
			// word-wrapped text needs refresh
			refresh = TRUE;
		}

		// centered images or text need refresh
		if (style & SS_CENTERIMAGE)
			refresh = TRUE;

		// simple text never needs refresh
		if (style & SS_TYPEMASK == SS_SIMPLE)
			refresh = FALSE;
	}

	// get dialog's and control's rect
	CRect wndrc, objrc;

	GetClientRect(&wndrc);
	::GetWindowRect(wnd, &objrc);
	ScreenToClient(&objrc);
	
	CSize tl_margin, br_margin;

	if (br_type == NOANCHOR)
		br_type = tl_type;
	
	// calculate margin for the top-left corner

	tl_margin.cx = objrc.left - wndrc.Width() * tl_type.cx / 100;
	tl_margin.cy = objrc.top - wndrc.Height() * tl_type.cy / 100;
	
	// calculate margin for the bottom-right corner

	br_margin.cx = objrc.right - wndrc.Width() * br_type.cx / 100;
	br_margin.cy = objrc.bottom - wndrc.Height() * br_type.cy / 100;

	// add to the list
	Layout obj(wnd, tl_type, tl_margin,	br_type, br_margin, hscroll, refresh);
	m_arrLayout.Add(obj);
}

void CResizableDialog::ArrangeLayout()
{
	// update size-grip
	InvalidateRect(&m_rcGripRect);
	UpdateGripPos();
	InvalidateRect(&m_rcGripRect);

	// init some vars
	CRect wndrc;
	GetClientRect(&wndrc);

	int i, count = m_arrLayout.GetSize();
	HDWP hdwp = BeginDeferWindowPos(count);

	for (i=0; i<count; ++i)
	{
		Layout& obj = m_arrLayout[i];

		CRect objrc, newrc;
		CWnd* wnd = CWnd::FromHandle(obj.hwnd); // temporary solution

		wnd->GetWindowRect(&objrc);
		ScreenToClient(&objrc);
		
		// calculate new top-left corner

		newrc.left = obj.tl_margin.cx + wndrc.Width() * obj.tl_type.cx / 100;
		newrc.top = obj.tl_margin.cy + wndrc.Height() * obj.tl_type.cy / 100;
		
		// calculate new bottom-right corner

		newrc.right = obj.br_margin.cx + wndrc.Width() * obj.br_type.cx / 100;
		newrc.bottom = obj.br_margin.cy + wndrc.Height() * obj.br_type.cy / 100;

		if (!newrc.EqualRect(&objrc))
		{
			if (obj.adj_hscroll)
			{
				// needs repainting, due to horiz scrolling
				int diff = newrc.Width() - objrc.Width();
				int max = wnd->GetScrollLimit(SB_HORZ);
			
				obj.need_refresh = FALSE;
				if (max > 0 && wnd->GetScrollPos(SB_HORZ) > max - diff)
				{
					obj.need_refresh = TRUE;
				}
			}

			// set flags 
			DWORD flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREPOSITION;
			if (newrc.TopLeft() == objrc.TopLeft())
				flags |= SWP_NOMOVE;
			if (newrc.Size() == objrc.Size())
				flags |= SWP_NOSIZE;
			
			DeferWindowPos(hdwp, obj.hwnd, NULL, newrc.left, newrc.top,
				newrc.Width(), newrc.Height(), flags);
		}
	}
	// go re-arrange child windows
	EndDeferWindowPos(hdwp);

	// refresh those that need
	for (i=0; i<count; ++i)
	{
		Layout& obj = m_arrLayout[i];
		CWnd* wnd = CWnd::FromHandle(obj.hwnd); // temporary solution
	
		if (obj.need_refresh)
		{
			wnd->Invalidate();
			wnd->UpdateWindow();
		}
	}
}

void CResizableDialog::UpdateGripPos()
{
	// size-grip goes bottom right in the client area

	GetClientRect(&m_rcGripRect);

	m_rcGripRect.left = m_rcGripRect.right - m_szGripSize.cx;
	m_rcGripRect.top = m_rcGripRect.bottom - m_szGripSize.cy;
}

// protected members

void CResizableDialog::ShowSizeGrip(BOOL bShow)
{
	if (m_bShowGrip != bShow)
	{
		m_bShowGrip = bShow;
		InvalidateRect(&m_rcGripRect);
	}
}

void CResizableDialog::SetMaximizedRect(const CRect& rc)
{
	m_bUseMaxRect = TRUE;

	m_ptMaxPos = rc.TopLeft();
	m_ptMaxSize.x = rc.Width();
	m_ptMaxSize.y = rc.Height();
}

void CResizableDialog::ResetMaximizedRect()
{
	m_bUseMaxRect = FALSE;
}

void CResizableDialog::SetMinTrackSize(const CSize& size)
{
	m_bUseMinTrack = TRUE;

	m_ptMinTrackSize.x = size.cx;
	m_ptMinTrackSize.y = size.cy;
}

void CResizableDialog::ResetMinTrackSize()
{
	m_bUseMinTrack = FALSE;
}

void CResizableDialog::SetMaxTrackSize(const CSize& size)
{
	m_bUseMaxTrack = TRUE;

	m_ptMaxTrackSize.x = size.cx;
	m_ptMaxTrackSize.y = size.cy;
}

void CResizableDialog::ResetMaxTrackSize()
{
	m_bUseMaxTrack = FALSE;
}

// NOTE: this must be called after all the other settings
//       to have the dialog and its controls displayed properly
void CResizableDialog::EnableSaveRestore(LPCTSTR pszSection, LPCTSTR pszEntry)
{
	m_sSection = pszSection;
	m_sEntry = pszEntry;

	m_bEnableSaveRestore = TRUE;

	LoadWindowRect();
}


// used to save/restore window's size and position
// either in the registry or a private .INI file
// depending on your application settings

#define PROFILE_FMT 	_T("%d,%d,%d,%d,%d,%d")

void CResizableDialog::SaveWindowRect()
{
	CString data;
	WINDOWPLACEMENT wp;

	ZeroMemory(&wp, sizeof(WINDOWPLACEMENT));
	wp.length = sizeof(WINDOWPLACEMENT);
	GetWindowPlacement(&wp);
	
	RECT& rc = wp.rcNormalPosition;	// alias

	data.Format(PROFILE_FMT, rc.left, rc.top,
		rc.right, rc.bottom, wp.showCmd, wp.flags);

	AfxGetApp()->WriteProfileString(m_sSection, m_sEntry, data);
}

void CResizableDialog::LoadWindowRect()
{
	CString data;
	WINDOWPLACEMENT wp;

	data = AfxGetApp()->GetProfileString(m_sSection, m_sEntry);
	
	if (data.IsEmpty())	// never saved before
		return;
	
	ZeroMemory(&wp, sizeof(WINDOWPLACEMENT));
	wp.length = sizeof(WINDOWPLACEMENT);
	GetWindowPlacement(&wp);

	RECT& rc = wp.rcNormalPosition;	// alias

	if (_stscanf(data, PROFILE_FMT, &rc.left, &rc.top,
		&rc.right, &rc.bottom, &wp.showCmd, &wp.flags) == 6)
	{
		SetWindowPlacement(&wp);
	}
}
