// StatsTab.cpp : implementation file
//

#include "stdafx.h"
#include "Icecast2win.h"
#include "StatsTab.h"

#include "Icecast2winDlg.h"

extern CIcecast2winDlg	*g_mainDialog;

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern void AddToAdditionalGlobalStats(CString source, CString name);

/////////////////////////////////////////////////////////////////////////////
// CStatsTab dialog


CStatsTab::CStatsTab(CWnd* pParent /*=NULL*/)
	: CTabPageSSL(CStatsTab::IDD, pParent)
{
	//{{AFX_DATA_INIT(CStatsTab)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
}


void CStatsTab::DoDataExchange(CDataExchange* pDX)
{
	CTabPageSSL::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CStatsTab)
	DDX_Control(pDX, IDC_STATIC_SLS, m_SLS);
	DDX_Control(pDX, IDC_STATSLIST, m_StatsListCtrl);
	DDX_Control(pDX, IDC_SOURCELIST, m_SourceListCtrl);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CStatsTab, CTabPageSSL)
	//{{AFX_MSG_MAP(CStatsTab)
	ON_NOTIFY(NM_DBLCLK, IDC_SOURCELIST, OnDblclkSourcelist)
	ON_NOTIFY(NM_RCLICK, IDC_STATSLIST, OnRclickStatslist)
	ON_NOTIFY(NM_CLICK, IDC_SOURCELIST, OnClickSourcelist)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CStatsTab message handlers

BOOL CStatsTab::OnInitDialog() 
{
	CTabPageSSL::OnInitDialog();
	
	// TODO: Add extra initialization here
	m_SourceListCtrl.InsertColumn(0, _T("Source"), LVCFMT_LEFT, m_colSource0Width);
	m_StatsListCtrl.InsertColumn(0, _T("Statistic"), LVCFMT_LEFT, m_colStats0Width);
	m_StatsListCtrl.InsertColumn(1, _T("Value"), LVCFMT_LEFT, m_colStats1Width);

//	AddAnchor(IDC_STATSLIST, TOP_LEFT, BOTTOM_RIGHT);
//	AddAnchor(IDC_SOURCELIST, TOP_LEFT, BOTTOM_LEFT);
//	AddAnchor(IDC_FILLER1, BOTTOM_LEFT, BOTTOM_RIGHT);
	
	m_SourceListCtrl.SetSelectionMark(0);
	m_SLS.SetFont(&(g_mainDialog->labelFont), TRUE);

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CStatsTab::OnDblclkSourcelist(NMHDR* pNMHDR, LRESULT* pResult) 
{
	// TODO: Add your control notification handler code here
	g_mainDialog->statsTab.m_StatsListCtrl.DeleteAllItems();
	g_mainDialog->UpdateStatsLists();
	*pResult = 0;
}

void CStatsTab::OnRclickStatslist(NMHDR* pNMHDR, LRESULT* pResult) 
{
	// TODO: Add your control notification handler code here
	CMenu	menu;

	
    CPoint point;                                            
    ::GetCursorPos(&point); //where is the mouse?

    DWORD dwSelectionMade;                                       
    menu.LoadMenu(IDR_MENU2);  
    CMenu *pmenuPopup = menu.GetSubMenu(0);
    dwSelectionMade = pmenuPopup->TrackPopupMenu( (TPM_LEFTALIGN|TPM_LEFTBUTTON|
                                                       TPM_NONOTIFY|TPM_RETURNCMD),
                                                       point.x, point.y, this);                                
  
    pmenuPopup->DestroyMenu();
	char	msg[255] ="";
	char	buffer[1024] = "";
	char	buffer2[1024] = "";

	CString name;
	CString source;
	POSITION pos;
	switch (dwSelectionMade) {
	case ID_POPUP_ADDTOGLOBALSTATLIST :
		pos = m_StatsListCtrl.GetFirstSelectedItemPosition();
		if (pos != NULL) {
			int nItem = m_StatsListCtrl.GetNextSelectedItem(pos);
			LVITEM	lvi;

			lvi.mask =  LVIF_TEXT;
			lvi.iItem = nItem;
			lvi.iSubItem = 0;
			lvi.pszText = buffer;
			lvi.cchTextMax = sizeof(buffer);
			m_StatsListCtrl.GetItem(&lvi);
			name = buffer;
	   }
		pos = m_SourceListCtrl.GetFirstSelectedItemPosition();
		if (pos != NULL) {
			int nItem = m_SourceListCtrl.GetNextSelectedItem(pos);
			LVITEM	lvi;

			lvi.mask =  LVIF_TEXT;
			lvi.iItem = nItem;
			lvi.iSubItem = 0;
			lvi.pszText = buffer2;
			lvi.cchTextMax = sizeof(buffer2);
			m_SourceListCtrl.GetItem(&lvi);
			source = buffer2;
	   }
		AddToAdditionalGlobalStats(source, name);
		break;
	default :
		break;
	}

	*pResult = 0;
}

void CStatsTab::OnClickSourcelist(NMHDR* pNMHDR, LRESULT* pResult) 
{
	// TODO: Add your control notification handler code here
	OnDblclkSourcelist(pNMHDR, pResult);
	*pResult = 0;
}
