// Status.cpp : implementation file
//

#include "stdafx.h"
#include "Icecast2win.h"
#include "Status.h"

#include "Icecast2winDlg.h"

extern CIcecast2winDlg	*g_mainDialog;

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern void RemoveFromAdditionalGlobalStats(CString source, CString name);
extern void AddToTitleAdditionalGlobalStats(CString source, CString name);


/////////////////////////////////////////////////////////////////////////////
// CStatus dialog


CStatus::CStatus(CWnd* pParent /*=NULL*/)
	: CTabPageSSL(CStatus::IDD, pParent)
{
	//{{AFX_DATA_INIT(CStatus)
	m_Clients = _T("");
	m_Sources = _T("");
	m_RunningFor = _T("");
	//}}AFX_DATA_INIT
}


void CStatus::DoDataExchange(CDataExchange* pDX)
{
	CTabPageSSL::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CStatus)
	DDX_Control(pDX, IDC_STATIC_GS, m_GS);
	DDX_Control(pDX, IDC_GLOBALSTAT_LIST, m_GlobalStatList);
	DDX_Text(pDX, IDC_RUNNINGFOR, m_RunningFor);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CStatus, CTabPageSSL)
	//{{AFX_MSG_MAP(CStatus)
	ON_NOTIFY(NM_RCLICK, IDC_GLOBALSTAT_LIST, OnRclickGlobalstatList)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CStatus message handlers

BOOL CStatus::OnInitDialog() 
{
	CTabPageSSL::OnInitDialog();


	m_GlobalStatList.InsertColumn(0, _T("Stat Type"), LVCFMT_LEFT, m_colStats0Width);
	m_GlobalStatList.InsertColumn(1, _T("Name"), LVCFMT_LEFT, m_colStats1Width);
	m_GlobalStatList.InsertColumn(2, _T("Value"), LVCFMT_LEFT, m_colStats2Width);

	m_GlobalStatList.SetExtendedStyle(LVS_EX_FULLROWSELECT);
	// TODO: Add extra initialization here
//	AddAnchor(IDC_FILLER2, BOTTOM_LEFT, BOTTOM_RIGHT);
//	AddAnchor(IDC_GLOBALSTAT_LIST, TOP_LEFT, BOTTOM_RIGHT);
//	AddAnchor(IDC_STATIC_RUN, BOTTOM_LEFT, BOTTOM_RIGHT);
//	AddAnchor(IDC_RUNNINGFOR, BOTTOM_LEFT, BOTTOM_RIGHT);
	
	m_GS.SetFont(&(g_mainDialog->labelFont), TRUE);

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CStatus::OnRclickGlobalstatList(NMHDR* pNMHDR, LRESULT* pResult) 
{
	// TODO: Add your control notification handler code here
	CMenu	menu;

	
    CPoint point;                                            
    ::GetCursorPos(&point); //where is the mouse?

    DWORD dwSelectionMade;                                       
    menu.LoadMenu(IDR_MENU3);  
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
	int nItem;
	switch (dwSelectionMade) {
	case ID__DELETEFROMGLOBALSTATS :
		pos = m_GlobalStatList.GetFirstSelectedItemPosition();
		if (pos != NULL) {
			nItem = m_GlobalStatList.GetNextSelectedItem(pos);
			LVITEM	lvi;

			lvi.mask =  LVIF_TEXT;
			lvi.iItem = nItem;
			lvi.iSubItem = 0;
			lvi.pszText = buffer;
			lvi.cchTextMax = sizeof(buffer);
			m_GlobalStatList.GetItem(&lvi);
			source = buffer;
			lvi.iSubItem = 1;
			lvi.pszText = buffer2;
			lvi.cchTextMax = sizeof(buffer2);
			m_GlobalStatList.GetItem(&lvi);
			name = buffer2;

			if (source == "Global Stat") {
				MessageBox("Sorry, but you can't delete this type of stat", NULL, MB_OK);
			}
			else {
				RemoveFromAdditionalGlobalStats(source, name);
				m_GlobalStatList.DeleteItem(nItem);
			}
	   }
		break;
	case ID__MAKETHISSTATTHEWINDOWTITLE :
		pos = m_GlobalStatList.GetFirstSelectedItemPosition();
		if (pos != NULL) {
			nItem = m_GlobalStatList.GetNextSelectedItem(pos);
			LVITEM	lvi;

			lvi.mask =  LVIF_TEXT;
			lvi.iItem = nItem;
			lvi.iSubItem = 0;
			lvi.pszText = buffer;
			lvi.cchTextMax = sizeof(buffer);
			m_GlobalStatList.GetItem(&lvi);
			source = buffer;
			lvi.iSubItem = 1;
			lvi.pszText = buffer2;
			lvi.cchTextMax = sizeof(buffer2);
			m_GlobalStatList.GetItem(&lvi);
			name = buffer2;

			AddToTitleAdditionalGlobalStats(source, name);
		}
		break;
	default :
		break;
	}
	
	*pResult = 0;
}
