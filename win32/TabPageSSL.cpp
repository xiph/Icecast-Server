#include "stdafx.h"
#include "TabPageSSL.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// Construction

CTabPageSSL::CTabPageSSL () {
#ifndef _AFX_NO_OCC_SUPPORT
	AfxEnableControlContainer ();
#endif // !_AFX_NO_OCC_SUPPORT
}

CTabPageSSL::CTabPageSSL (UINT nIDTemplate, CWnd* pParent /*=NULL*/)
	: CDialog(nIDTemplate, pParent) {
#ifndef _AFX_NO_OCC_SUPPORT
	AfxEnableControlContainer ();
#endif // !_AFX_NO_OCC_SUPPORT
}

/////////////////////////////////////////////////////////////////////////////
// Destruction

CTabPageSSL::~CTabPageSSL () {
}

/////////////////////////////////////////////////////////////////////////////
// Message Handlers

void CTabPageSSL::OnOK (void) {
	//
	// Prevent CDialog::OnOK from calling EndDialog.
	//
}

void CTabPageSSL::OnCancel (void) {
	//
	// Prevent CDialog::OnCancel from calling EndDialog.
	//
}

BOOL CTabPageSSL::OnCommand (WPARAM wParam, LPARAM lParam) {
	// Call base class OnCommand to allow message map processing
	CDialog::OnCommand (wParam, lParam);
	//
	// Forward WM_COMMAND messages to the dialog's parent.
	//
	return GetParent ()->SendMessage (WM_COMMAND, wParam, lParam);
}

BOOL CTabPageSSL::OnNotify (WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
	//
	// Forward WM_NOTIFY messages to the dialog's parent.
	//
	CDialog::OnNotify (wParam, lParam, pResult);
	return GetParent ()->SendMessage (WM_NOTIFY, wParam, lParam);
}

BOOL CTabPageSSL::OnCmdMsg (UINT nID, int nCode, void* pExtra,
	AFX_CMDHANDLERINFO* pHandlerInfo) {
	//
	// Forward ActiveX control events to the dialog's parent.
	//
#ifndef _AFX_NO_OCC_SUPPORT
	if (nCode == CN_EVENT)
		return GetParent ()->OnCmdMsg (nID, nCode, pExtra, pHandlerInfo);
#endif // !_AFX_NO_OCC_SUPPORT

	return CDialog::OnCmdMsg (nID, nCode, pExtra, pHandlerInfo);
}
