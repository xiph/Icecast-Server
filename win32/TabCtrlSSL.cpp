// TabCtrlSSL.cpp : implementation file
//

#include "stdafx.h"
#include "TabCtrlSSL.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// Construction

CTabCtrlSSL::CTabCtrlSSL () {
#ifndef _AFX_NO_OCC_SUPPORT
	AfxEnableControlContainer ();
#endif // !_AFX_NO_OCC_SUPPORT
}

/////////////////////////////////////////////////////////////////////////////
// Destruction

CTabCtrlSSL::~CTabCtrlSSL (void) {
}

BEGIN_MESSAGE_MAP(CTabCtrlSSL, CTabCtrl)
	//{{AFX_MSG_MAP(CTabCtrlSSL)
	ON_WM_DESTROY ()
	ON_WM_SETFOCUS ()
	ON_WM_KILLFOCUS ()
	ON_NOTIFY_REFLECT (TCN_SELCHANGING, OnSelChanging)
	ON_NOTIFY_REFLECT (TCN_SELCHANGE, OnSelChange)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// Page Functions

int CTabCtrlSSL::AddSSLPage (LPCTSTR pszTitle, int nPageID, CTabPageSSL* pTabPage) {
	// Add a page to the tab control.
    TabDelete tabDelete;
    tabDelete.pTabPage = pTabPage;
    tabDelete.bDelete = FALSE;

    return AddPage (pszTitle, nPageID, tabDelete);
}

int CTabCtrlSSL::AddSSLPage (LPCTSTR pszTitle, int nPageID, LPCTSTR pszTemplateName) {
	// Verify that the dialog template is compatible with CTabCtrlSSL
	// (debug builds only). If your app asserts here, make sure the dialog
	// resource you're adding to the view is a borderless child window and
	// is not marked visible.
#ifdef _DEBUG
	if (pszTemplateName != NULL) {
		BOOL bResult = CheckDialogTemplate (pszTemplateName);
		ASSERT (bResult);
	}
#endif // _DEBUG

	// Add a page to the tab control.
	// Create a modeless dialog box.
	CTabPageSSL* pDialog = new CTabPageSSL;

	if (pDialog == NULL) {
		return -1;
	}

	if (!pDialog->Create (pszTemplateName, this)) {
		pDialog->DestroyWindow ();
		delete pDialog;
		return -1;
	}
    
    TabDelete tabDelete;
    tabDelete.pTabPage = pDialog;
    tabDelete.bDelete = TRUE;

    return AddPage (pszTitle, nPageID, tabDelete);
}

int CTabCtrlSSL::AddSSLPage (LPCTSTR pszTitle, int nPageID, int nTemplateID) {
	return AddSSLPage (pszTitle, nPageID, MAKEINTRESOURCE (nTemplateID));
}

BOOL CTabCtrlSSL::RemoveSSLPage (int nIndex) {
	if (nIndex >= GetItemCount ())
		return FALSE;

	// Notify derived classes that the page is being destroyed.
	OnDestroyPage (nIndex, m_nPageIDs[nIndex]);

	// Switch pages if the page being deleted is the current page and it's
	// not the only remaining page.
	int nCount = GetItemCount ();
	if (nCount > 1 && nIndex == GetCurSel ()) {
		int nPage = nIndex + 1;
		if (nPage >= nCount)
			nPage = nCount - 2;
		ActivateSSLPage (nPage);
	}

	// Remove the page from the tab control.
	DeleteItem (nIndex);

	// Destroy the dialog (if any) that represents the page.
    TabDelete tabDelete = m_tabs[nIndex];
    CTabPageSSL* pDialog = tabDelete.pTabPage;
	if (pDialog != NULL) {
		pDialog->DestroyWindow ();	
		delete pDialog;
	}

	// Clean up, repaint, and return.
	m_tabs.RemoveAt (nIndex);
	m_hFocusWnd.RemoveAt (nIndex);
	m_nPageIDs.RemoveAt (nIndex);
	Invalidate ();
	return TRUE;
}

int CTabCtrlSSL::GetSSLPageCount (void) {
	return GetItemCount ();
}

BOOL CTabCtrlSSL::GetSSLPageTitle (int nIndex, CString &strTitle) {
	if (nIndex >= GetItemCount ())
		return FALSE;

	TCHAR szTitle[1024];

	TC_ITEM item;
	item.mask = TCIF_TEXT;
	item.pszText = szTitle;
	item.cchTextMax = sizeof szTitle / sizeof (TCHAR);

	if (!GetItem (nIndex, &item))
		return FALSE;

	strTitle = item.pszText;
	return TRUE;
}

BOOL CTabCtrlSSL::SetSSLPageTitle (int nIndex, LPCTSTR pszTitle) {
	if (nIndex >= GetItemCount ())
		return FALSE;

	TC_ITEM item;
	item.mask = TCIF_TEXT;
	item.pszText = (LPTSTR) pszTitle;
	
	BOOL bResult = SetItem (nIndex, &item);
	if (bResult)
		Invalidate ();
	return bResult;
}

int CTabCtrlSSL::GetSSLPageID (int nIndex) {
	if (nIndex >= GetItemCount ())
		return -1;

	return m_nPageIDs[nIndex];
}

int CTabCtrlSSL::SetSSLPageID (int nIndex, int nPageID) {
	if (nIndex >= GetItemCount ())
		return -1;

	int nOldPageID = m_nPageIDs[nIndex];
	m_nPageIDs[nIndex] = nPageID;
	return nOldPageID;
}

BOOL CTabCtrlSSL::ActivateSSLPage (int nIndex) {
	if (nIndex >= GetItemCount ())
		return FALSE;

	// Do nothing if the specified page is already active.
	if (nIndex == GetCurSel ())
		return TRUE;

	// Deactivate the current page.
	int nOldIndex = GetCurSel ();

	if (nIndex != -1) {
        TabDelete tabDelete = m_tabs[nOldIndex];
        CTabPageSSL* pDialog = tabDelete.pTabPage;
		if (pDialog != NULL) {
			m_hFocusWnd[nOldIndex] = ::GetFocus ();
			pDialog->ShowWindow (SW_HIDE);
		}
	}

	// Activate the new one.
	SetCurSel (nIndex);
    TabDelete tabDelete = m_tabs[nIndex];
    CTabPageSSL* pDialog = tabDelete.pTabPage;

	if (pDialog != NULL) {
		::SetFocus (m_hFocusWnd[nIndex]);
		CRect rect;
		GetClientRect (&rect);
		ResizeDialog (nIndex, rect.Width (), rect.Height ());
		pDialog->ShowWindow (SW_SHOW);
	}
	return TRUE;
}

int CTabCtrlSSL::GetSSLActivePage (void) {
	return GetCurSel ();
}

CWnd* CTabCtrlSSL::GetSSLPage (int nIndex) {
	if (nIndex >= GetItemCount ())
		return NULL;

    TabDelete tabDelete = m_tabs[nIndex];
    return (CWnd*) tabDelete.pTabPage;
}

int CTabCtrlSSL::GetSSLPageIndex (int nPageID) {
	int nCount = GetItemCount ();
	if (nCount == 0)
		return -1;

	for (int i=0; i<nCount; i++) {
		if (m_nPageIDs[i] == nPageID)
			return i;
	}

	return -1;
}

/////////////////////////////////////////////////////////////////////////////
// Private helper functions

#ifdef _DEBUG
BOOL CTabCtrlSSL::CheckDialogTemplate (LPCTSTR pszTemplateName) {
	// Verify that the dialog resource exists.
	ASSERT (pszTemplateName != NULL);
	HINSTANCE hInstance = AfxFindResourceHandle (pszTemplateName, RT_DIALOG);
	HRSRC hResource = ::FindResource (hInstance, pszTemplateName, RT_DIALOG);

	if (hResource == NULL)
		return FALSE; // Resource doesn't exist

	HGLOBAL hTemplate = LoadResource (hInstance, hResource);
	ASSERT (hTemplate != NULL);

	// Get the dialog's style bits.
	DLGTEMPLATEEX* pTemplate = (DLGTEMPLATEEX*) LockResource (hTemplate);

	DWORD dwStyle;
	if (pTemplate->signature == 0xFFFF)
		dwStyle = pTemplate->style;
	else
		dwStyle = ((DLGTEMPLATE*) pTemplate)->style;

	UnlockResource (hTemplate);
	FreeResource (hTemplate);

	// Verify that the dialog is an invisible child window.
	if (dwStyle & WS_VISIBLE)
		return FALSE; // WS_VISIBLE flag is set

	if (!(dwStyle & WS_CHILD))
		return FALSE; // WS_CHILD flag isn't set

	// Verify that the dialog has no border and no title bar.
	if (dwStyle & (WS_BORDER | WS_THICKFRAME | DS_MODALFRAME))
		return FALSE; // One or more border flags are set

	if (dwStyle & WS_CAPTION)
		return FALSE; // WS_CAPTION flag is set

	return TRUE;
}
#endif // _DEBUG

void CTabCtrlSSL::ResizeDialog (int nIndex, int cx, int cy) {
	if (nIndex != -1) {
        TabDelete tabDelete = m_tabs[nIndex];
        CTabPageSSL* pDialog = tabDelete.pTabPage;

		if (pDialog != NULL) {
			CRect rect;
			GetItemRect (nIndex, &rect);

			int x, y, nWidth, nHeight;
			DWORD dwStyle = GetStyle ();

			if (dwStyle & TCS_VERTICAL) { // Vertical tabs
				int nTabWidth =
					rect.Width () * GetRowCount ();
				x = (dwStyle & TCS_RIGHT) ? 4 : nTabWidth + 4;
				y = 4;
				nWidth = cx - nTabWidth - 8;
				nHeight = cy - 8;
			}
			else { // Horizontal tabs
				int nTabHeight =
					rect.Height () * GetRowCount ();
				x = 4;
				y = (dwStyle & TCS_BOTTOM) ? 4 : nTabHeight + 4;
				nWidth = cx - 8;
				nHeight = cy - nTabHeight - 8;
				
				

			}
			pDialog->SetWindowPos (NULL, x, y, nWidth, nHeight, SWP_NOZORDER);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////
// Overridables

BOOL CTabCtrlSSL::OnInitPage (int nIndex, int nPageID) {
	// TODO: Override in derived class to initialise pages.
	return TRUE;
}

void CTabCtrlSSL::OnActivatePage (int nIndex, int nPageID) {
	// TODO: Override in derived class to respond to page activations.
}

void CTabCtrlSSL::OnDeactivatePage (int nIndex, int nPageID) {
	// TODO: Override in derived class to respond to page deactivations.
}

void CTabCtrlSSL::OnDestroyPage (int nIndex, int nPageID) {
	// TODO: Override in derived class to free resources.
}

/////////////////////////////////////////////////////////////////////////////
// Message handlers

void CTabCtrlSSL::OnSelChanging (NMHDR* pNMHDR, LRESULT* pResult) {
	// Notify derived classes that the selection is changing.
	int nIndex = GetCurSel ();
	if (nIndex == -1)
		return;

	OnDeactivatePage (nIndex, m_nPageIDs[nIndex]);

	// Save the input focus and hide the old page.
    TabDelete tabDelete = m_tabs[nIndex];
    CTabPageSSL* pDialog = tabDelete.pTabPage;

	if (pDialog != NULL) {
		m_hFocusWnd[nIndex] = ::GetFocus ();
		pDialog->ShowWindow (SW_HIDE);
	}
	*pResult = 0;
}

void CTabCtrlSSL::OnSelChange (NMHDR* pNMHDR, LRESULT* pResult) {
	int nIndex = GetCurSel ();
	if (nIndex == -1)
		return;

	// Show the new page.
    TabDelete tabDelete = m_tabs[nIndex];
    CTabPageSSL* pDialog = tabDelete.pTabPage;

	if (pDialog != NULL) {
		::SetFocus (m_hFocusWnd[nIndex]);
		CRect rect;
		GetClientRect (&rect);
		ResizeDialog (nIndex, rect.Width (), rect.Height ());
		pDialog->ShowWindow (SW_SHOW);
	}

	// Notify derived classes that the selection has changed.
	OnActivatePage (nIndex, m_nPageIDs[nIndex]);
	*pResult = 0;
}

void CTabCtrlSSL::OnSetFocus (CWnd* pOldWnd) {
	CTabCtrl::OnSetFocus (pOldWnd);
	
	// Set the focus to a control on the current page.
	int nIndex = GetCurSel ();
	if (nIndex != -1)
		::SetFocus (m_hFocusWnd[nIndex]);	
}

void CTabCtrlSSL::OnKillFocus (CWnd* pNewWnd) {
	CTabCtrl::OnKillFocus (pNewWnd);
	
	// Save the HWND of the control that holds the input focus.
	int nIndex = GetCurSel ();
	if (nIndex != -1)
		m_hFocusWnd[nIndex] = ::GetFocus ();	
}

// My thanks to Tomasz Sowinski for all his help coming up with a workable
// solution to the stack versus heap object destruction
void CTabCtrlSSL::OnDestroy (void) {
	int nCount = m_tabs.GetSize ();

	// Destroy dialogs and delete CTabCtrlSSL objects.
	if (nCount > 0) {
		for (int i=nCount - 1; i>=0; i--) {
			OnDestroyPage (i, m_nPageIDs[i]);
            TabDelete tabDelete = m_tabs[i];
            CTabPageSSL* pDialog = tabDelete.pTabPage;
			if (pDialog != NULL) {
				pDialog->DestroyWindow ();
                if (TRUE == tabDelete.bDelete) {
                    delete pDialog;
                }
			}
		}
	}

	// Clean up the internal arrays.
	m_tabs.RemoveAll ();
	m_hFocusWnd.RemoveAll ();
	m_nPageIDs.RemoveAll ();

	CTabCtrl::OnDestroy ();
}

BOOL CTabCtrlSSL::OnCommand (WPARAM wParam, LPARAM lParam) {
	// Forward WM_COMMAND messages to the dialog's parent.
	return GetParent ()->SendMessage (WM_COMMAND, wParam, lParam);
}

BOOL CTabCtrlSSL::OnNotify (WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
	// Forward WM_NOTIFY messages to the dialog's parent.
	return GetParent ()->SendMessage (WM_NOTIFY, wParam, lParam);
}

BOOL CTabCtrlSSL::OnCmdMsg (UINT nID, int nCode, void* pExtra,
	AFX_CMDHANDLERINFO* pHandlerInfo) {
	// Forward ActiveX control events to the dialog's parent.
#ifndef _AFX_NO_OCC_SUPPORT
	if (nCode == CN_EVENT)
		return GetParent ()->OnCmdMsg (nID, nCode, pExtra, pHandlerInfo);
#endif // !_AFX_NO_OCC_SUPPORT

	return CTabCtrl::OnCmdMsg (nID, nCode, pExtra, pHandlerInfo);
}

int CTabCtrlSSL::AddPage (LPCTSTR pszTitle, int nPageID, TabDelete tabDelete) {
	// Add a page to the tab control.
	TC_ITEM item;
	item.mask = TCIF_TEXT;
	item.pszText = (LPTSTR) pszTitle;
	int nIndex = GetItemCount ();
	
	if (InsertItem (nIndex, &item) == -1)
		return -1;

    if (NULL == tabDelete.pTabPage) {
		// Fail - no point calling the function with a NULL pointer!
		DeleteItem (nIndex);
		return -1;
	}
	else {
		// Record the address of the dialog object and the page ID.
		int nArrayIndex = m_tabs.Add (tabDelete);
		ASSERT (nIndex == nArrayIndex);

		nArrayIndex = m_nPageIDs.Add (nPageID);
		ASSERT (nIndex == nArrayIndex);

		// Size and position the dialog box within the view.
        tabDelete.pTabPage->SetParent (this); // Just to be sure

		CRect rect;
		GetClientRect (&rect);

		if (rect.Width () > 0 && rect.Height () > 0)
			ResizeDialog (nIndex, rect.Width (), rect.Height ());

		// Initialize the page.
		if (OnInitPage (nIndex, nPageID)) {
			// Make sure the first control in the dialog is the one that
			// receives the input focus when the page is displayed.
			HWND hwndFocus = tabDelete.pTabPage->GetTopWindow ()->m_hWnd;
			nArrayIndex = m_hFocusWnd.Add (hwndFocus);
			ASSERT (nIndex == nArrayIndex);
		}
		else {
			// Make the control that currently has the input focus is the one
			// that receives the input focus when the page is displayed.
			m_hFocusWnd.Add (::GetFocus ());
		}

		// If this is the first page added to the view, make it visible.
		if (nIndex == 0)
			tabDelete.pTabPage->ShowWindow (SW_SHOW);
	}
	return nIndex;
}
