#if !defined(AFX_TABPAGESSL_H__619331B3_7DE7_4DB1_A039_2103E87E8E71__INCLUDED_)
#define AFX_TABPAGESSL_H__619331B3_7DE7_4DB1_A039_2103E87E8E71__INCLUDED_

/////////////////////////////////////////////////////////////////////////////
// CTabPageSSL declaration

class CTabPageSSL : public CDialog
{
public:
// Construction
	CTabPageSSL ();	// Default Constructor
	CTabPageSSL (UINT nIDTemplate, CWnd* pParent = NULL);	// Standard Constructor
// Destruction
	~CTabPageSSL ();

protected:
// Message Handlers
	virtual BOOL OnCommand (WPARAM wParam, LPARAM lParam);
	virtual BOOL OnNotify (WPARAM wParam, LPARAM lParam, LRESULT* pResult);
	virtual void OnOK (void);
	virtual void OnCancel (void);
	virtual BOOL OnCmdMsg (UINT nID, int nCode, void* pExtra,
		AFX_CMDHANDLERINFO* pHandlerInfo);
};

#endif // !defined(AFX_TABPAGE_H__619331B3_7DE7_4DB1_A039_2103E87E8E71__INCLUDED_)
