// Icecast2winDlg.h : header file
//

#if !defined(AFX_ICECAST2WINDLG_H__23B4DA8B_C9BC_49C8_A62C_37FC6BC5E54A__INCLUDED_)
#define AFX_ICECAST2WINDLG_H__23B4DA8B_C9BC_49C8_A62C_37FC6BC5E54A__INCLUDED_

#include "TabCtrlSSL.h"
#include "TabPageSSL.h"

#include "ConfigTab.h"
#include "StatsTab.h"
#include "Status.h"
#include "TrayNot.h"

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

/////////////////////////////////////////////////////////////////////////////
// CIcecast2winDlg dialog

class CIcecast2winDlg : public CDialog
{
// Construction
public:
	time_t serverStart;
	void config_read();
	void config_write();
	void UpdateStatsLists();
	CConfigTab	configTab;
	CStatsTab	statsTab;
	CStatus		statusTab;
	int	m_colSource0Width;
	int	m_colStats0Width;
	int	m_colStats1Width;
	int	m_colGStats0Width;
	int	m_colGStats1Width;
	int	m_colGStats2Width;
	CFont labelFont;
	CBitmap runningBitmap;
	CBitmap stoppedBitmap;
	CTrayNot* m_pTray;
	BOOL m_bHidden;
	int  m_iconSwap;





	void StopServer();
	bool m_isRunning;
	void DisableControl(UINT control);
	void EnableControl(UINT control);
	void getTag(char *pbuf, char *ptag, char *dest);
	CString m_ErrorLog;
	CString m_AccessLog;
	void ParseConfig();
	void LoadConfig();
	CIcecast2winDlg(CWnd* pParent = NULL);	// standard constructor

// Dialog Data
	//{{AFX_DATA(CIcecast2winDlg)
	enum { IDD = IDD_ICECAST2WIN_DIALOG };
	CStatic	m_SS;
	CStatic	m_ServerStatusBitmap;
	CStatic	m_iceLogo;
	CButton	m_StartButton;
	CEdit	m_StatsEditCtrl;
	CEdit	m_ConfigEditCtrl;
	CEdit	m_ErrorEditCtrl;
	CEdit	m_AccessEditCtrl;
	CTabCtrlSSL	m_MainTab;
	CString	m_AccessEdit;
	CString	m_ErrorEdit;
	CString	m_ConfigEdit;
	CString	m_ServerStatus;
	CString	m_SourcesConnected;
	CString	m_NumClients;
	FILE	*filep_accesslog;
	FILE	*filep_errorlog;
	CString	m_StatsEdit;
	BOOL	m_Autostart;
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CIcecast2winDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	HICON m_hIcon;

	// Generated message map functions
	//{{AFX_MSG(CIcecast2winDlg)
	virtual BOOL OnInitDialog();
	virtual void OnCancel();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnSelchangeMaintab(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnFileExit();
	afx_msg void OnTimer(UINT nIDEvent);
	afx_msg void OnFileStartserver();
	afx_msg void OnFileStopserver();
	afx_msg void OnStart();
	afx_msg void OnClose();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnHidesystray();
	afx_msg void OnHide();
	afx_msg void OnBlankRestore();
	afx_msg LONG OnTrayNotify ( WPARAM wParam, LPARAM lParam );
	afx_msg void OnDestroy();
	afx_msg void OnFileEditconfiguration();
	afx_msg void OnAboutHelp();
	afx_msg void OnAboutCredits();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_ICECAST2WINDLG_H__23B4DA8B_C9BC_49C8_A62C_37FC6BC5E54A__INCLUDED_)
