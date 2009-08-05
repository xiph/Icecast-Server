// Icecast2winDlg.cpp : implementation file
//

#include "stdafx.h"
#include "Icecast2win.h"
#include "Icecast2winDlg.h"
#include <process.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <stdlib.h>
#include <curl/curl.h>

extern "C" {
#include "thread/thread.h"
#include "avl/avl.h"
#include "log/log.h"
#include "global.h"
#include "httpp/httpp.h"
#include "net/sock.h"
#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "stats.h"
#include "xslt.h"
}

#include <afxinet.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define ICECAST_VERSION "2.x"
CEdit	*g_accessControl;
CEdit	*g_errorControl;
CIcecast2winDlg	*g_mainDialog;
bool	g_tailAccess = false;
bool	g_tailError = false;
CString gConfigurationSave;

char	gTitleSource[1024] = "";
char	gTitleName[1024] = "";

#define MAXSTATSPERSOURCE 60
#define MAXSOURCES 1024

typedef struct tagElement {
	CString	name;
	CString	value;
	int		titleFlag;
} Element;

typedef struct tagElementAdditional {
	CString source;
	CString	name;
	CString	value;
	int		titleFlag;
} ElementAdditional;


typedef struct tagMainElement {
	CString	source;
	long	numStats;
	Element	stats[MAXSTATSPERSOURCE];
	int		populated;
} MainElement;

typedef struct tagMainElementAdditional {
	long	numStats;
	ElementAdditional	stats[MAXSTATSPERSOURCE];
} MainElementAdditional;


MainElement gStats[MAXSOURCES];
MainElement gGlobalStats;
MainElementAdditional	gAdditionalGlobalStats;

long	numMainStats;

extern "C" {
	int main(int argc, char **argv);
}


void AddToAdditionalGlobalStats(CString source, CString name) {
	int foundit = 0;
	for (int i=0;i<gAdditionalGlobalStats.numStats;i++) {
		if ((gAdditionalGlobalStats.stats[i].source == source) && (gAdditionalGlobalStats.stats[i].name == name)) {
			foundit = 1;
			break;
		}
	}
	if (!foundit) {
		gAdditionalGlobalStats.stats[gAdditionalGlobalStats.numStats].source = source;
		gAdditionalGlobalStats.stats[gAdditionalGlobalStats.numStats].name = name;
		gAdditionalGlobalStats.numStats++;
	}
	g_mainDialog->UpdateStatsLists();
}

void ClearTitleAdditionalGlobalStats(CString source, CString name) {
	int foundit = 0;
	int i,j;
	for (i=0;i<gAdditionalGlobalStats.numStats;i++) {
		gAdditionalGlobalStats.stats[i].titleFlag = 0;
	}
	for (i=0;i<numMainStats;i++) {
		for (j=0;j<gStats[i].numStats;j++) {
			if ((gStats[i].source == source) && (gStats[i].stats[j].name == name)) {
				gStats[i].stats[j].titleFlag = 0;
			}
		}
	}
	g_mainDialog->UpdateStatsLists();
}
void AddToTitleAdditionalGlobalStats(CString source, CString name) {
	int foundit = 0;
	int i,j;
	for (i=0;i<gAdditionalGlobalStats.numStats;i++) {
		if ((gAdditionalGlobalStats.stats[i].source == source) && (gAdditionalGlobalStats.stats[i].name == name)) {
			ClearTitleAdditionalGlobalStats(source, name);
			gAdditionalGlobalStats.stats[i].titleFlag = 1;
			strcpy(gTitleSource, source);
			strcpy(gTitleName, name);
			foundit = 1;
			break;
		}
	}
	if (!foundit) {
		for (i=0;i<numMainStats;i++) {
			for (j=0;j<gStats[i].numStats;j++) {
				if ((gStats[i].source == source) && (gStats[i].stats[j].name == name)) {
					ClearTitleAdditionalGlobalStats(source, name);
					gStats[i].stats[j].titleFlag = 1;
					strcpy(gTitleSource, source);
					strcpy(gTitleName, name);
					foundit = 1;
					break;
				}
			}
		}
	}
	g_mainDialog->UpdateStatsLists();
}

void RemoveFromAdditionalGlobalStats(CString source, CString name) {
	int foundit = 0;
	for (int i=0;i<gAdditionalGlobalStats.numStats;i++) {
		if ((gAdditionalGlobalStats.stats[i].source == source) && (gAdditionalGlobalStats.stats[i].name == name)) {
			for (int j=i+1;j < gAdditionalGlobalStats.numStats;j++) {
				gAdditionalGlobalStats.stats[j-1].name = gAdditionalGlobalStats.stats[j].name;
				gAdditionalGlobalStats.stats[j-1].value = gAdditionalGlobalStats.stats[j].value;
				gAdditionalGlobalStats.stats[j-1].source = gAdditionalGlobalStats.stats[j].source;
			}
			gAdditionalGlobalStats.numStats--;
			break;
		}
	}
	g_mainDialog->UpdateStatsLists();
}
/////////////////////////////////////////////////////////////////////////////
// CAboutDlg dialog used for App About

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// Dialog Data
	//{{AFX_DATA(CAboutDlg)
	enum { IDD = IDD_ABOUTBOX };
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAboutDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	//{{AFX_MSG(CAboutDlg)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAboutDlg)
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
	//{{AFX_MSG_MAP(CAboutDlg)
		// No message handlers
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


/////////////////////////////////////////////////////////////////////////////
// CIcecast2winDlg dialog

CIcecast2winDlg::CIcecast2winDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CIcecast2winDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CIcecast2winDlg)
	m_AccessEdit = _T("");
	m_ErrorEdit = _T("");
	m_ConfigEdit = _T("");
	m_ServerStatus = _T("");
	m_SourcesConnected = _T("");
	m_NumClients = _T("");
	m_StatsEdit = _T("");
	m_Autostart = FALSE;
	//}}AFX_DATA_INIT
	// Note that LoadIcon does not require a subsequent DestroyIcon in Win32
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	m_pTray = NULL;
}

void CIcecast2winDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CIcecast2winDlg)
	DDX_Control(pDX, IDC_STATIC_SS, m_SS);
	DDX_Control(pDX, IDC_SERVERSTATUS, m_ServerStatusBitmap);
	DDX_Control(pDX, IDC_START, m_StartButton);
	DDX_Control(pDX, IDC_MAINTAB, m_MainTab);
	DDX_Check(pDX, IDC_AUTOSTART, m_Autostart);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CIcecast2winDlg, CDialog)
	//{{AFX_MSG_MAP(CIcecast2winDlg)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_NOTIFY(TCN_SELCHANGE, IDC_MAINTAB, OnSelchangeMaintab)
	ON_COMMAND(ID_FILE_EXIT, OnFileExit)
	ON_WM_TIMER()
	ON_COMMAND(ID_FILE_STARTSERVER, OnFileStartserver)
	ON_COMMAND(ID_FILE_STOPSERVER, OnFileStopserver)
	ON_BN_CLICKED(IDC_START, OnStart)
	ON_WM_CLOSE()
	ON_WM_SIZE()
	ON_BN_CLICKED(IDC_HIDESYSTRAY, OnHidesystray)
	ON_COMMAND(ID_BLANK_RESTORE, OnBlankRestore)
	ON_MESSAGE(WM_TRAY_NOTIFY, OnTrayNotify)
	ON_WM_DESTROY()
	ON_COMMAND(ID_FILE_EDITCONFIGURATION, OnFileEditconfiguration)
	ON_COMMAND(ID_ABOUT_HELP, OnAboutHelp)
	ON_COMMAND(ID_ABOUT_CREDITS, OnAboutCredits)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CIcecast2winDlg message handlers

#include "colors.h"


BOOL CIcecast2winDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		CString strAboutMenu;
		strAboutMenu.LoadString(IDS_ABOUTBOX);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	g_mainDialog = this;

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon
	
	// TODO: Add extra initialization here
	config_read();

	statsTab.m_colSource0Width = m_colSource0Width;
	statsTab.m_colStats0Width = m_colStats0Width;
	statsTab.m_colStats1Width = m_colStats1Width;
	statusTab.m_colStats0Width = m_colGStats0Width;
	statusTab.m_colStats1Width = m_colGStats1Width;
	statusTab.m_colStats2Width = m_colGStats2Width;
	
	statsTab.Create(IDD_STATSDIALOG, this);
	statusTab.Create(IDD_SSTATUS, this);

	int nPageID = 0;
	m_MainTab.AddSSLPage (_T("Server Status"), nPageID, (CTabPageSSL *)&statusTab);
	nPageID++;
	m_MainTab.AddSSLPage (_T("Source Level Stats"), nPageID, (CTabPageSSL *)&statsTab);
	nPageID++;

	
	labelFont.CreateFont(24,0, 0, 0, FW_BOLD, 0, 0, 0, 0, OUT_TT_PRECIS, 0, PROOF_QUALITY, 0, "Arial");
	
	runningBitmap.LoadBitmap(IDB_BITMAP6);
	stoppedBitmap.LoadBitmap(IDB_BITMAP5);

	UpdateData(FALSE);

	LoadConfig();

//	AddAnchor(IDC_MAINTAB, TOP_LEFT, BOTTOM_RIGHT);
//	AddAnchor(IDC_STATICBLACK, TOP_LEFT, TOP_RIGHT);

//	EnableSaveRestore("icecast2win", "positions");

	m_pTray = NULL;

	char	version[255] = "";
	sprintf(version, "Icecast2 Version %s", ICECAST_VERSION);
	SetWindowText(version);

	if (m_Autostart) {
		OnStart();
	}
	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CIcecast2winDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialog::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CIcecast2winDlg::OnPaint() 
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, (WPARAM) dc.GetSafeHdc(), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}


// The system calls this to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CIcecast2winDlg::OnQueryDragIcon()
{
	return (HCURSOR) m_hIcon;
}

void CIcecast2winDlg::OnSelchangeMaintab(NMHDR* pNMHDR, LRESULT* pResult) 
{
	// TODO: Add your control notification handler code here

	*pResult = 0;
}

void CIcecast2winDlg::LoadConfig()
{
	FILE	*filep;
	char	buffer[2046] = "";
	CIcecast2winApp	*myApp = (CIcecast2winApp *)AfxGetApp();

	configTab.m_Config = "";
	filep = fopen(myApp->m_configFile, "r");
	if (filep) {
		fclose(filep);
	}
	else {
		MessageBox("Unable to load config file (" + CString(myApp->m_configFile) + ") unable to start", NULL, MB_OK);
	}

}


void CIcecast2winDlg::OnFileExit() 
{
	// TODO: Add your command handler code here
	DestroyWindow();
}



void CIcecast2winDlg::getTag(char *pbuf, char *ptag, char *dest)
{
	char	openTag[256] = "";
	char	closeTag[256] = "";

	sprintf(openTag, "<%s>", ptag);
	sprintf(closeTag, "</%s>", ptag);
	
	char *p1;
	p1 = strstr(pbuf, openTag);
	if (p1) {
		p1 = p1 + strlen(openTag);
		char *p2;
		p2 = strstr(p1, closeTag);
		if (p2) {
			strncpy(dest, p1, p2-p1);
		}
	}
}


void CIcecast2winDlg::EnableControl(UINT control)
{
	CWnd	*pWnd;
	pWnd = GetDlgItem(control);
	::ShowWindow(pWnd->GetSafeHwnd(), SW_SHOW);
}

void CIcecast2winDlg::DisableControl(UINT control)
{
	CWnd	*pWnd;
	pWnd = GetDlgItem(control);
	::ShowWindow(pWnd->GetSafeHwnd(), SW_HIDE);
}


void AddUpdateStatistic(int sourceIndex, char *name, char *value)
{
	for (int j=0;j<gStats[sourceIndex].numStats;j++) {
		if (gStats[sourceIndex].stats[j].name == name) {
			gStats[sourceIndex].stats[j].value = value;
			return;
		}
	}
	int numStats = gStats[sourceIndex].numStats;
    if (numStats >= MAXSTATSPERSOURCE)
        return;
	/* If we get here, we haven't found the stat, so add it */
	gStats[sourceIndex].stats[numStats].name = name;
	gStats[sourceIndex].stats[numStats].value = value;
	gStats[sourceIndex].numStats++;

}
int GetSourceIndex(char *sourceName)
{
	if (sourceName == NULL) {
		return 0;
	}
	for (int i=1;i<numMainStats+1;i++) {
		if (!strcmp(gStats[i].source, sourceName)) {
			return i;
		}
	}
    if (numMainStats >= MAXSOURCES)
        return 0;
	/* This means we haven't seen the source, so lets add it */
	numMainStats++;
	gStats[numMainStats].source = sourceName;
	gStats[numMainStats].populated = 1;
	gStats[numMainStats].numStats = 0;
	return numMainStats;

}
void UpdateSourceData(xmlDocPtr doc, xmlNodePtr cur, char *sourceName) {
	xmlNodePtr children;
	char	*ls_xmlContentPtr = NULL;
	int sourceIndex = GetSourceIndex(sourceName);
	int listenerInd = 0;

	children = cur->xmlChildrenNode;
	while (children != NULL) {
		if (!strcmp((char *)children->name, "listeners")) {
			listenerInd = 1;
		}
		ls_xmlContentPtr = (char *)xmlNodeListGetString(doc, children->xmlChildrenNode, 1);
		AddUpdateStatistic(sourceIndex, (char *)children->name, ls_xmlContentPtr);
		xmlFree(ls_xmlContentPtr);
		children = children->next;
	}
	if (!listenerInd) {
		/* If no listeners, then the source has been disconnected */
		gStats[sourceIndex].populated = 0;
		gStats[sourceIndex].numStats = 0;
		g_mainDialog->statsTab.m_SourceListCtrl.DeleteAllItems();
		g_mainDialog->statsTab.m_StatsListCtrl.DeleteAllItems();
	}
}

bool g_collectingStats = false;

void StartStats(void *dummy)
{
	while (global.running != ICE_RUNNING) {
		Sleep(500);
	}
	while (global.running == ICE_RUNNING) {
		if (global.running == ICE_RUNNING) {
			for (int j=0;j<MAXSOURCES;j++) {
				gStats[j].numStats = 0;
			}
			numMainStats = 0;

			xmlDocPtr doc;

			doc = stats_get_xml(0, NULL);
			xmlNodePtr cur;
		    cur = xmlDocGetRootElement(doc); 

		    if (cur == NULL) {
				MessageBox(NULL, "empty XML document", "Error", MB_OK);
				xmlFreeDoc(doc);
				return;
			}

			cur = cur->xmlChildrenNode;
			char* ls_xmlContentPtr2 = NULL;

		    while (cur != NULL) {
				if ((!xmlStrcmp(cur->name, (const xmlChar *)"source"))) {
					ls_xmlContentPtr2 = (char *)xmlGetProp(cur, (unsigned char *)"mount");
					UpdateSourceData(doc, cur, ls_xmlContentPtr2);
				}
				else {
					/* A Global stat */
					ls_xmlContentPtr2 = (char *)xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
					AddUpdateStatistic(0, (char *)cur->name, ls_xmlContentPtr2);
				}
				if (ls_xmlContentPtr2) {
					xmlFree(ls_xmlContentPtr2);
				}

				cur = cur->next;
			}
			xmlFreeDoc(doc);
			g_mainDialog->UpdateStatsLists();
			Sleep(5000);
		}
		if (global.running != ICE_RUNNING) {
			numMainStats = 0;
			g_mainDialog->statusTab.m_GlobalStatList.DeleteAllItems();
			g_mainDialog->statsTab.m_SourceListCtrl.DeleteAllItems();
			g_mainDialog->statsTab.m_StatsListCtrl.DeleteAllItems();
			_endthread();
		}
	}
	_endthread();
}
void CIcecast2winDlg::OnTimer(UINT nIDEvent) 
{
	// TODO: Add your message handler code here and/or call default
	if (nIDEvent == 0) {
		if (global.running == ICE_RUNNING) {
			char	buffer[255] = "";
			CString	tmp;
			// Get info from stats...
			m_ServerStatusBitmap.SetBitmap(HBITMAP(runningBitmap));
			sprintf(buffer, "%d", global.sources);
			tmp = buffer;
			if (tmp != statusTab.m_Sources) {
				statusTab.m_Sources = tmp;
				statusTab.UpdateData(FALSE);
			}
			sprintf(buffer, "%d", global.clients);
			tmp = buffer;
			if (tmp != statusTab.m_Clients) {
				statusTab.m_Clients = tmp;
				statusTab.UpdateData(FALSE);
			}

			m_StartButton.GetWindowText(tmp);

			if (tmp == "Start Server") {
				m_StartButton.SetWindowText("Stop Server");
				m_StartButton.SetState(0);
			}
			time_t	currentTime;
			time(&currentTime);
			time_t  runningTime = currentTime - serverStart;

			CTimeSpan runningFor(runningTime);

			char	timespan[1024] = "";
			sprintf(timespan, "%d Days, %d Hours, %d Minutes, %d Seconds", runningFor.GetDays(), runningFor.GetHours(), runningFor.GetMinutes(), runningFor.GetSeconds());
			statusTab.m_RunningFor = timespan;
			statusTab.UpdateData(FALSE);

			SetTimer(0, 500, NULL);
		}
		else {
			statusTab.m_Sources = "0";
			statusTab.m_Clients = "0";
			m_ServerStatusBitmap.SetBitmap(HBITMAP(stoppedBitmap));
			m_StartButton.SetWindowText("Start Server");
			m_StartButton.SetState(0);
			UpdateData(FALSE);
			statusTab.m_RunningFor = "Not running";
			statusTab.UpdateData(FALSE);
		}
	}
	
	CDialog::OnTimer(nIDEvent);
}

char	g_configFile[1024] = "";
char	g_progName[255] = "icecast2";

void StartServer(void *configfile)
{
	int		argc = 3;
	char*	argv[3];

	strcpy(g_configFile, (char *)configfile);

	argv[0] = g_progName;
	argv[1] = "-c";
	argv[2] = g_configFile;
	time(&(g_mainDialog->serverStart));

	int ret = main(argc, (char **)argv);
	if (ret) {
		MessageBox(NULL, "Unable to start server", NULL, MB_OK);
	}
	global.running = ICE_HALTING;
	_endthread();


}
void CIcecast2winDlg::OnFileStartserver() 
{
	// TODO: Add your command handler code here
	CIcecast2winApp	*myApp = (CIcecast2winApp *)AfxGetApp();

	if (gConfigurationSave == "") {
		gConfigurationSave = m_ConfigEdit;
	}

	if (global.running == ICE_RUNNING) {
		MessageBox("Server already running", "Error", MB_OK);
	}
	else {
		m_ConfigEditCtrl.SetReadOnly(TRUE);
		LoadConfig();
		SetTimer(0, 500, NULL);
		_beginthread(StartServer, 0, (void *)(LPCSTR)myApp->m_configFile);
		_beginthread(StartStats, 0, (void *)NULL);
	}
}

void CIcecast2winDlg::OnFileStopserver() 
{
	// TODO: Add your command handler code here
	;
}

bool infocus = false;

void CIcecast2winDlg::StopServer()
{
	KillTimer(0);
	global.running = ICE_HALTING;
	m_StartButton.SetWindowText("Start Server");
	m_StartButton.SetState(0);
	m_ServerStatusBitmap.SetBitmap(HBITMAP(stoppedBitmap));
	statusTab.m_RunningFor = "Not running";
	statusTab.UpdateData(FALSE);



}


void CIcecast2winDlg::OnStart() 
{
	CIcecast2winApp	*myApp = (CIcecast2winApp *)AfxGetApp();

	// TODO: Add your control notification handler code here

	if (global.running == ICE_RUNNING) {
		StopServer();
	}
	else {
		SetTimer(0, 500, NULL);
		_beginthread(StartServer, 0, (void *)(LPCSTR)myApp->m_configFile);
		_beginthread(StartStats, 0, (void *)NULL);
	}
	
}

void CIcecast2winDlg::UpdateStatsLists()
{
	char	item[1024] = "";
	int l = 0;

	// Global Stats are index of 0
	for (int k=0;k < gStats[0].numStats;k++) {
		int inthere = 0;
		for (l=0;l < statusTab.m_GlobalStatList.GetItemCount();l++) {

			statusTab.m_GlobalStatList.GetItemText(l, 1, item, sizeof(item));
			if (!strcmp(gStats[0].stats[k].name, item)) {
				inthere = 1;
				break;
			}
		}
		if (!inthere) {
			LVITEM	lvi;

			lvi.mask =  LVIF_IMAGE | LVIF_TEXT;
			lvi.iItem = statsTab.m_SourceListCtrl.GetItemCount();
			lvi.iSubItem = 0;
			//lvi.pszText = (LPTSTR)(LPCTSTR)gStats[0].source;
			lvi.pszText = "Global Stat";
			statusTab.m_GlobalStatList.InsertItem(&lvi);
			lvi.iSubItem = 1;
			lvi.pszText = (LPTSTR)(LPCTSTR)gStats[0].stats[k].name;
			statusTab.m_GlobalStatList.SetItem(&lvi);
			lvi.iSubItem = 2;
			lvi.pszText = (LPTSTR)(LPCTSTR)gStats[0].stats[k].value;
			statusTab.m_GlobalStatList.SetItem(&lvi);
			if ((!strcmp(gTitleSource, gStats[0].source)) && 
				(!strcmp(gTitleName, gStats[0].stats[k].name))) {
				gStats[0].stats[k].titleFlag = 1;
			}

		}
		else {
			LVITEM	lvi;

			lvi.mask =  LVIF_IMAGE | LVIF_TEXT;
			lvi.iItem = l;
			lvi.iSubItem = 2;
			lvi.pszText = (LPTSTR)(LPCTSTR)gStats[0].stats[k].value;
			statusTab.m_GlobalStatList.SetItem(&lvi);
		}
		if (gStats[0].stats[k].titleFlag) {
			CString	windowTitle = CString("Global Stat") + " - " + gStats[0].stats[k].name + " - " + gStats[0].stats[k].value;
			SetWindowText(windowTitle);
			if (m_pTray) {
				m_pTray->SetTIP((LPSTR)(LPCSTR)windowTitle);
			}
		}
	}

	for (int i=1;i<numMainStats+1;i++) {
		int inthere = 0;
		int k = 0;
		if (gStats[i].populated) {
			for (l=0;l < gAdditionalGlobalStats.numStats;l++) {
				for (int m=0;m < gStats[i].numStats;m++) {
					if ((gAdditionalGlobalStats.stats[l].source == gStats[i].source) &&
						(gAdditionalGlobalStats.stats[l].name == gStats[i].stats[m].name)) {
						gAdditionalGlobalStats.stats[l].value = gStats[i].stats[m].value;
						break;
					}
				}
			}
			if (strcmp(gStats[i].source, "Global Stat")) {
				// If Not Global STat
				for (k=0;k < statsTab.m_SourceListCtrl.GetItemCount();k++) {

					statsTab.m_SourceListCtrl.GetItemText(k, 0, item, sizeof(item));
					if (!strcmp(gStats[i].source, item)) {
						inthere = 1;
						break;
					}
				}
				if (!inthere) {
					if (gStats[i].source != "") {
						LVITEM	lvi;

						lvi.mask =  LVIF_IMAGE | LVIF_TEXT;
						lvi.iItem = statsTab.m_SourceListCtrl.GetItemCount();
						lvi.iSubItem = 0;
						lvi.pszText = (LPTSTR)(LPCTSTR)gStats[i].source;
						statsTab.m_SourceListCtrl.InsertItem(&lvi);
					}
				}
				int nItemSelected = statsTab.m_SourceListCtrl.GetSelectionMark();
				if (nItemSelected != -1) {
					memset(item, '\000', sizeof(item));
					statsTab.m_SourceListCtrl.GetItemText(nItemSelected, 0, item, sizeof(item));
					if (!strcmp(gStats[i].source, item)) {
						for (int l=0;l<gStats[i].numStats;l++) {
							int inthere2 = 0;
							char	item2[1024] = "";
							for (int m=0;m < statsTab.m_StatsListCtrl.GetItemCount();m++) {
								statsTab.m_StatsListCtrl.GetItemText(m, 0, item2, sizeof(item2));
								if (!strcmp(gStats[i].stats[l].name, item2)) {
									LVITEM	lvi;

									lvi.mask =  LVIF_TEXT;
									lvi.iItem = m;
									lvi.iSubItem = 1;
									lvi.pszText = (LPTSTR)(LPCTSTR)gStats[i].stats[l].value;
									statsTab.m_StatsListCtrl.SetItem(&lvi);
									inthere2 = 1;
									break;
								}
							}
							if (!inthere2) {
								LVITEM	lvi;

								lvi.mask =  LVIF_TEXT;
								lvi.iItem = statsTab.m_StatsListCtrl.GetItemCount();
								lvi.iSubItem = 0;
								lvi.pszText = (LPTSTR)(LPCTSTR)gStats[i].stats[l].name;
								statsTab.m_StatsListCtrl.InsertItem(&lvi);
								lvi.iSubItem = 1;
								lvi.pszText = (LPTSTR)(LPCTSTR)gStats[i].stats[l].value;
								statsTab.m_StatsListCtrl.SetItem(&lvi);
							}
						}
					}
				}
				for (l=0;l < gAdditionalGlobalStats.numStats;l++) {
					int inthere2 = 0;
					char	item2[1024] = "";
					char	item3[1024] = "";
					CString	itemSource;
					CString itemName;
					for (int m=0;m < statusTab.m_GlobalStatList.GetItemCount();m++) {
						statusTab.m_GlobalStatList.GetItemText(m, 0, item2, sizeof(item2));
						statusTab.m_GlobalStatList.GetItemText(m, 1, item3, sizeof(item3));
						itemSource = item2;
						itemName = item3;
						if ((gAdditionalGlobalStats.stats[l].source == itemSource) &&
							(gAdditionalGlobalStats.stats[l].name == itemName)) {
							LVITEM	lvi;

							lvi.mask =  LVIF_TEXT;
							lvi.iItem = m;
							lvi.iSubItem = 2;
							lvi.pszText = (LPTSTR)(LPCTSTR)gAdditionalGlobalStats.stats[l].value;
							statusTab.m_GlobalStatList.SetItem(&lvi);
							inthere2 = 1;
							break;
						}
					}
					if (!inthere2) {
						LVITEM	lvi;


						lvi.mask =  LVIF_TEXT;
						lvi.iItem = statusTab.m_GlobalStatList.GetItemCount();
						lvi.iSubItem = 0;
						lvi.pszText = (LPTSTR)(LPCTSTR)gAdditionalGlobalStats.stats[l].source;
						statusTab.m_GlobalStatList.InsertItem(&lvi);
						lvi.iSubItem = 1;
						lvi.pszText = (LPTSTR)(LPCTSTR)gAdditionalGlobalStats.stats[l].name;
						statusTab.m_GlobalStatList.SetItem(&lvi);
						lvi.iSubItem = 2;
						lvi.pszText = (LPTSTR)(LPCTSTR)gAdditionalGlobalStats.stats[l].value;
						statusTab.m_GlobalStatList.SetItem(&lvi);
						if ((!strcmp(gTitleSource, gAdditionalGlobalStats.stats[l].source)) && 
							(!strcmp(gTitleName, gAdditionalGlobalStats.stats[l].name))) {
							gAdditionalGlobalStats.stats[l].titleFlag = 1;
						}
							

					}

					if (gAdditionalGlobalStats.stats[l].titleFlag) {
						CString	windowTitle = gAdditionalGlobalStats.stats[l].source + " - " + gAdditionalGlobalStats.stats[l].name + " - " + gAdditionalGlobalStats.stats[l].value;
						SetWindowText(windowTitle);
						if (m_pTray) {
							m_pTray->SetTIP((LPSTR)(LPCSTR)windowTitle);
						}
					}
				}
			}
			else {
			}
		}
	}
}

char	gAppName[255] = "icecast2";
char	gConfigFile[255] = "icecast2.ini";

void CIcecast2winDlg::config_write()
{
	char	buf[255] = "";
	char	buf2[1024] = "";

	UpdateData(TRUE);

	m_colSource0Width = statsTab.m_SourceListCtrl.GetColumnWidth(0);
	m_colStats0Width = statsTab.m_StatsListCtrl.GetColumnWidth(0);
	m_colStats1Width = statsTab.m_StatsListCtrl.GetColumnWidth(1);
	m_colGStats0Width = statusTab.m_GlobalStatList.GetColumnWidth(0);
	m_colGStats1Width = statusTab.m_GlobalStatList.GetColumnWidth(1);
	m_colGStats2Width = statusTab.m_GlobalStatList.GetColumnWidth(2);


	sprintf(buf, "%d", m_colSource0Width);
	WritePrivateProfileString(gAppName, "col0SourceWidth", buf, gConfigFile);
	sprintf(buf, "%d", m_colStats0Width);
	WritePrivateProfileString(gAppName, "col0StatsWidth", buf, gConfigFile);
	sprintf(buf, "%d", m_colStats1Width);
	WritePrivateProfileString(gAppName, "col1StatsWidth", buf, gConfigFile);
	sprintf(buf, "%d", m_colGStats0Width);
	WritePrivateProfileString(gAppName, "col0GStatsWidth", buf, gConfigFile);
	sprintf(buf, "%d", m_colGStats1Width);
	WritePrivateProfileString(gAppName, "col1GStatsWidth", buf, gConfigFile);
	sprintf(buf, "%d", m_colGStats2Width);
	WritePrivateProfileString(gAppName, "col2GStatsWidth", buf, gConfigFile);

	if (m_Autostart) {
		WritePrivateProfileString(gAppName, "AutoStart", "1", gConfigFile);
	}
	else {
		WritePrivateProfileString(gAppName, "AutoStart", "0", gConfigFile);
	}

	sprintf(buf, "%d", gAdditionalGlobalStats.numStats);
	WritePrivateProfileString(gAppName, "numAdditionalStats", buf, gConfigFile);

	for (int i=0;i<gAdditionalGlobalStats.numStats;i++) {
		memset(buf, '\000', sizeof(buf));
		sprintf(buf2, "AdditionalStatsSource%d", i);
		WritePrivateProfileString(gAppName, buf2, gAdditionalGlobalStats.stats[i].source, gConfigFile);

		memset(buf, '\000', sizeof(buf));
		sprintf(buf2, "AdditionalStatsName%d", i);
		WritePrivateProfileString(gAppName, buf2, gAdditionalGlobalStats.stats[i].name, gConfigFile);

		if (gAdditionalGlobalStats.stats[i].titleFlag) {
			sprintf(buf2, "%s|%s", gAdditionalGlobalStats.stats[i].source, gAdditionalGlobalStats.stats[i].name);
			WritePrivateProfileString(gAppName, "TitleName", buf2, gConfigFile);
		}
	}
	for (i=0;i<numMainStats;i++) {
		for (int k=0;k < gStats[i].numStats;k++) {
			if (gStats[i].stats[k].titleFlag) {
				sprintf(buf2, "%s|%s", gStats[i].source, gStats[i].stats[k].name);
				WritePrivateProfileString(gAppName, "TitleName", buf2, gConfigFile);
			}
		}
	}

}

void CIcecast2winDlg::config_read()
{
	char	buf2[1024] = "";
	char	buf[1024] = "";
	CString	tempString;

	m_colSource0Width = GetPrivateProfileInt(gAppName, "col0SourceWidth", 163, gConfigFile);
	m_colStats0Width = GetPrivateProfileInt(gAppName, "col0StatsWidth", 100, gConfigFile);
	m_colStats1Width = GetPrivateProfileInt(gAppName, "col1StatsWidth", 150, gConfigFile);
	m_colGStats0Width = GetPrivateProfileInt(gAppName, "col0GStatsWidth", 150, gConfigFile);
	m_colGStats1Width = GetPrivateProfileInt(gAppName, "col1GStatsWidth", 150, gConfigFile);
	m_colGStats2Width = GetPrivateProfileInt(gAppName, "col2GStatsWidth", 150, gConfigFile);

	GetPrivateProfileString(gAppName, "AutoStart", "0", buf, sizeof(buf), gConfigFile);
	if (!strcmp(buf, "1")) {
		m_Autostart = true;
	}
	else{
		m_Autostart = false;
	}
	int numAdditionalGlobalStats = GetPrivateProfileInt(gAppName, "numAdditionalStats", 0, gConfigFile);
	for (int i=0;i<numAdditionalGlobalStats;i++) {
		memset(buf, '\000', sizeof(buf));
		sprintf(buf2, "AdditionalStatsSource%d", i);
		GetPrivateProfileString(gAppName, buf2, "", buf, sizeof(buf), gConfigFile);
		gAdditionalGlobalStats.stats[i].source = buf;

		memset(buf, '\000', sizeof(buf));
		sprintf(buf2, "AdditionalStatsName%d", i);
		GetPrivateProfileString(gAppName, buf2, "", buf, sizeof(buf), gConfigFile);
		gAdditionalGlobalStats.stats[i].name = buf;
		gAdditionalGlobalStats.numStats++;
	}
	GetPrivateProfileString(gAppName, "TitleName", "", buf, sizeof(buf), gConfigFile);

	if (strlen(buf) > 0) {
		char *p1 = strchr(buf, '|');
		if (p1) {
			char tmpSource[1024] = "";
			char tmpName[1024] = "";
			memset(tmpSource, '\000', sizeof(tmpSource));
			memset(tmpName, '\000', sizeof(tmpName));
			

			strncpy(tmpSource, buf, p1-buf);
			p1++;
			strcpy(tmpName, p1);

			
			strcpy(gTitleSource, tmpSource);
			strcpy(gTitleName, tmpName);
		}
	}

}

void CIcecast2winDlg::OnClose() 
{
	// TODO: Add your message handler code here and/or call default
	config_write();
	CDialog::OnClose();
}

void CIcecast2winDlg::OnSize(UINT nType, int cx, int cy) 
{
	CDialog::OnSize(nType, cx, cy);
	
	int border1 = 0;
	int border2 = 78;
	// TODO: Add your message handler code here
	if (m_MainTab.m_hWnd) {
		CRect rect;
		GetClientRect (&rect);
		int x = rect.Width()-border1;
		int y = rect.Width()-border2;

		statusTab.SetWindowPos(NULL, 4, 22, cx, cy, SWP_NOZORDER);
		statsTab.SetWindowPos(NULL, 4, 22, cx, cy, SWP_NOZORDER);
		statusTab.m_GlobalStatList.SetWindowPos(NULL, 14, 55, cx-40, cy-180, SWP_NOZORDER);
		statsTab.m_StatsListCtrl.SetWindowPos(NULL, 213, 55, cx-243, cy-180, SWP_NOZORDER);
		statsTab.m_SourceListCtrl.SetWindowPos(NULL, 14, 55, 166, cy-180, SWP_NOZORDER);
//			CListCtrl	m_StatsListCtrl;
//	CListCtrl	m_SourceListCtrl;
		m_MainTab.SetWindowPos(NULL, 0, 80, cx, cy, SWP_NOZORDER);

		//m_MainTab.ResizeDialog(0, rect.Width()-border1, rect.Height()-border2);
		//m_MainTab.ResizeDialog(1, rect.Width()-border1, rect.Height()-border2);
	}

}


LONG CIcecast2winDlg::OnTrayNotify ( WPARAM wParam, LPARAM lParam )
{
	switch (lParam) {
	case WM_RBUTTONDOWN:
			{
			CMenu menu ;
			// Load and Verify Menu
			VERIFY(menu.LoadMenu(IDR_TRAY));
			CMenu* pPopup = menu.GetSubMenu (0) ;
			ASSERT(pPopup != NULL);

			// Get the cursor position
			POINT pt ;
			GetCursorPos (&pt) ;

			// Fix Microsofts' BUG!!!!
			SetForegroundWindow();

			///////////////////////////////////
			// Display The Menu
			pPopup->TrackPopupMenu(TPM_LEFTALIGN |
			TPM_RIGHTBUTTON,pt.x, pt.y, AfxGetMainWnd());
			break ;
			}
	case WM_LBUTTONDBLCLK:
	//////////////////////////////////
	// Unhide our Window
		if (m_bHidden) {
			ShowWindow (SW_RESTORE);
		}
	//OnUnHide() ;
		break ;
	}

	return (0) ;
}

void CIcecast2winDlg::OnHidesystray() 
{
	// TODO: Add your control notification handler code here
	OnHide();
	theApp.HideApplication();	
}
void CIcecast2winDlg::OnHide() 
{
	// TODO: Add your control notification handler code here
	if (m_pTray == NULL) {
		m_pTray = new CTrayNot (this,WM_TRAY_NOTIFY, NULL,theApp.m_pIconList);
	}
	m_pTray->SetState(0);
	m_bHidden = TRUE;
	
}

void CIcecast2winDlg::OnBlankRestore() 
{
	// TODO: Add your command handler code here
		if (m_bHidden) {
			ShowWindow (SW_RESTORE);
		}
	
}

void CIcecast2winDlg::OnDestroy() 
{
	CDialog::OnDestroy();
	
	if (m_pTray) {
		delete m_pTray ;
		m_pTray = NULL ;
	} 
	// TODO: Add your message handler code here
	
}

void CIcecast2winDlg::OnFileEditconfiguration() 
{
	// TODO: Add your command handler code here
	
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory( &si, sizeof(si) );
    si.cb = sizeof(si);
    ZeroMemory( &pi, sizeof(pi) );


	int ok = 1;
	if (global.running == ICE_RUNNING) {
		MessageBox("I'm sorry, but you cannot edit the configuration file while the server is running", NULL, MB_OK);
	}
	else {
		// Start the child process. 
		if( !CreateProcess( NULL, // No module name (use command line). 
			"notepad icecast.xml", // Command line. 
			NULL,             // Process handle not inheritable. 
			NULL,             // Thread handle not inheritable. 
			FALSE,            // Set handle inheritance to FALSE. 
			0,                // No creation flags. 
			NULL,             // Use parent's environment block. 
			NULL,             // Use parent's starting directory. 
			&si,              // Pointer to STARTUPINFO structure.
			&pi )             // Pointer to PROCESS_INFORMATION structure.
		) 
		{
		   ok = 0;
		}

		// Wait until child process exits.
		WaitForSingleObject( pi.hProcess, INFINITE );

		// Close process and thread handles. 
		CloseHandle( pi.hProcess );
		CloseHandle( pi.hThread );
	}

}

void CIcecast2winDlg::OnAboutHelp() 
{
	// TODO: Add your command handler code here
	ShellExecute(NULL, "open", "doc\\icecast2.chm", NULL, NULL, SW_SHOWNORMAL);	
}

void CIcecast2winDlg::OnAboutCredits() 
{
	// TODO: Add your command handler code here
	CAboutDlg	about;
	about.DoModal();
}

void CIcecast2winDlg::OnCancel()
{
}

