#include <atlstr.h>
#include <windows.h>
#include <tchar.h>
#include <exdisp.h>
#include <exdispid.h>
#include <mshtml.h>
#include <mshtmdid.h>
#include <shlwapi.h>
#include <stdio.h>
#include<comutil.h>


HINSTANCE hInstance;
LONG gref=0;

const TCHAR ClassName[] = _T("MainWindowClass");
const TCHAR DialogClassName[] = _T("DialogClass");
HWND hWndDlgBox;
HWND hDlgButton;
HWND hWndButton;

const CLSID BhoCLSID = {0xC9C42510,0x9B41,0x42c1,0x9D,0xCD,0x72,0x82,0xA2,0xD0,0x7C,0x61};
#define BhoCLSIDs  _T("{C9C42510-9B41-42c1-9DCD-7282A2D07C61}")

#define MY_MESSAGE ( _T("MY_MESSAGE") )
static UINT MY_EVENT = ::RegisterWindowMessage(MY_MESSAGE);

#define MY_PLAY ( _T("MY_PLAY") )
static UINT MY_PLAY_EVENT = ::RegisterWindowMessage(MY_PLAY);

#define MY_PAUSE ( _T("MY_PAUSE") )
static UINT MY_PAUSE_EVENT = ::RegisterWindowMessage(MY_PAUSE);

static int m_nRand;

// memory-mapped file name
const LPCTSTR sMemoryFileName = _T("D9287E19-6F9E-45fa-897C-D392F73A0F2F");

static IWebBrowser2* g_webBrowser = NULL;

DWORD WINAPI Thread_ProcEvent( LPVOID lpParam ); 





	LRESULT CALLBACK DlgProc( HWND   hWndDlg, UINT   Msg, WPARAM wParam, LPARAM lParam )
	{
		switch (Msg)
    {
        case WM_CREATE:
        {
            hDlgButton = CreateWindowEx(
            0,     
            _T("BUTTON"),
            _T("Close"), 
            WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 
            10, 
            10, 
            100,
            20, 
            hWndDlg, 
            0,
            (HINSTANCE)GetWindowLong(hWndDlg, GWL_HINSTANCE), 
            NULL); 

            if (!hDlgButton)
                MessageBox(NULL, _T("Dialog Button Failed."), _T("Error"), MB_OK | MB_ICONERROR);

            return TRUE;
        }
        break;
		
		case WM_COMMAND: 
        {
            switch(LOWORD(wParam))
            {
                case 1:
                {
                    switch(HIWORD(wParam))
					{
	                       case BN_CLICKED:
                           SendMessage(hWndDlg, WM_CLOSE, 0, 0);
                        break; 
                    }
                }
                break;
            }
            return 0;
        } 
        break; 

        case WM_CLOSE:
            DestroyWindow(hWndDlg);
            hWndDlgBox = NULL;
        break;

        default:
            return (DefWindowProc(hWndDlg, Msg, wParam, lParam));

    }

}







class BHO : public IObjectWithSite, public IDispatch 
{ 
	
	long ref;
	IWebBrowser2* webBrowser;
	IHTMLDocument* doc; IHTMLDocument2 *doc2;
	IHTMLWindow2 *win2;
	HANDLE m_hThread;
	HANDLE m_hFileMapping;
	LPVOID m_pViewOfFile;

	IDispatch* pHtmlDocDispatch;
	CComPtr<IHTMLDocument2>	pHtmlDoc;

	public:
	// IUnknown...
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) {if (riid==IID_IUnknown) *ppv=static_cast<BHO*>(this); else if (riid==IID_IObjectWithSite) *ppv=static_cast<IObjectWithSite*>(this); else if (riid==IID_IDispatch) *ppv=static_cast<IDispatch*>(this); else return E_NOINTERFACE; AddRef(); return S_OK;} 
	ULONG STDMETHODCALLTYPE AddRef() {InterlockedIncrement(&gref); return InterlockedIncrement(&ref);}
	ULONG STDMETHODCALLTYPE Release() {int tmp=InterlockedDecrement(&ref); if (tmp==0) delete this; InterlockedDecrement(&gref); return tmp;}

	// IDispatch... 
	HRESULT STDMETHODCALLTYPE GetTypeInfoCount(unsigned int FAR* pctinfo) {*pctinfo=1; return NOERROR;}
	HRESULT STDMETHODCALLTYPE GetTypeInfo(unsigned int iTInfo, LCID lcid, ITypeInfo FAR* FAR*  ppTInfo) {return NOERROR;}
	HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID riid, OLECHAR FAR* FAR* rgszNames, unsigned int cNames, LCID lcid, DISPID FAR* rgDispId) {return NOERROR;}
  
	HRESULT STDMETHODCALLTYPE Invoke(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS FAR* pDispParams, VARIANT FAR* pVarResult, EXCEPINFO FAR* pExcepInfo, unsigned int FAR* puArgErr)
	{ 
		// DISPID_DOCUMENTCOMPLETE: This is the earliest point we can obtain the "document" interface
		if (dispIdMember==DISPID_DOCUMENTCOMPLETE)
		{ 
			if (!webBrowser) return E_FAIL; 
			IDispatch *idisp; webBrowser->get_Document(&idisp);

			TCHAR strDbg[0x100] = {0};
			wsprintf(strDbg, _T(" document completed webbrowser=%X"), (LPVOID)webBrowser);
			OutputDebugString(strDbg);

			if (idisp && !doc) idisp->QueryInterface(IID_IHTMLDocument, (void**)&doc);
			if (idisp && !doc2) idisp->QueryInterface(IID_IHTMLDocument2, (void**)&doc2);
			if (doc2 && !win2) doc2->get_parentWindow(&win2);
			IConnectionPointContainer *cpc=0; if (doc) doc->QueryInterface(IID_IConnectionPointContainer, (void**) &cpc);
			IConnectionPoint* cp=0; if (cpc) cpc->FindConnectionPoint(DIID_HTMLDocumentEvents2, &cp);
			DWORD cookie; HRESULT hr; if (cp) hr=cp->Advise(static_cast<IDispatch*>(this), &cookie);
			if (cp) cp->Release(); if (cpc) cpc->Release(); if (idisp) idisp->Release();
			if (!doc || !doc2 || !win2 || hr!=S_OK) 
			{
				release(); return E_FAIL;
			}

			OutputDebugString(_T("______ Debug DISPID_DOCUMENTCOMPLETE"));

			m_hThread = NULL;
			CompairCurrentUrlWithList(); 
			g_webBrowser = webBrowser;

			return NOERROR;
		}
		if(dispIdMember==DISPID_BEFORENAVIGATE2)
		{
			OutputDebugString(_T("______ Debug DISPID_BEFORENAVIGATE2"));
		}
		if(dispIdMember==DISPID_NEWWINDOW2)
		{
			OutputDebugString(_T("______ Debug DISPID_NEWWINDOW2"));
		}
		if(dispIdMember==DISPID_NAVIGATECOMPLETE2)
		{
			OutputDebugString(_T("______ Debug DISPID_NAVIGATECOMPLETE2"));
		}
		if(dispIdMember==DISPID_ONQUIT)
		{
			OutputDebugString(_T("______ Debug DISPID_ONQUIT"));
			ExitProceThread();
		}
		if(dispIdMember==DISPID_ONVISIBLE)
		{
			OutputDebugString(_T("______ Debug DISPID_ONVISIBLE"));
		}
		if(dispIdMember==DISPID_ONTOOLBAR)
		{
			OutputDebugString(_T("______ Debug DISPID_ONTOOLBAR"));
		}
		if(dispIdMember==DISPID_ONTHEATERMODE)
		{
			OutputDebugString(_T("______ Debug DISPID_ONTHEATERMODE"));
		}
		if(dispIdMember==DISPID_WINDOWCLOSING )
		{
			OutputDebugString(_T("______ Debug DISPID_WINDOWCLOSING "));
		}
		if(dispIdMember==DISPID_NEWWINDOW3)
		{
			OutputDebugString(_T("______ Debug DISPID_NEWWINDOW3"));
		}
		if(dispIdMember==DISPID_WINDOWSTATECHANGED )
		{
			//OutputDebugString(_T("______ Debug DISPID_WINDOWSTATECHANGED "));
		}




/*
		if (dispIdMember==DISPID_HTMLDOCUMENTEVENTS_ONCLICK)
		{ 
			// This shows how to respond to simple events.
		
			//char *myCharArray=NULL;
			//myCharArray=_com_util::ConvertBSTRToString(*bstrTmp);
			//MessageBox(0,myCharArray,_T("BHO"),MB_OK); 


			// GetThreadID

			DWORD dwID = GetCurrentThreadId();
			TCHAR str[0x100]={0};
			wsprintf(str,_T("-------- debug threadid=%X"),dwID);

			OutputDebugString(str);

			SendMessage(HWND_BROADCAST ,MY_EVENT,0,0);
			//
			//CompairCurrentUrlWithList();

			//ExecuteScript("alert('David is the best');");
			//Navigate2ToOurpage();
 
			return NOERROR;
		}

		if (dispIdMember==DISPID_HTMLDOCUMENTEVENTS_ONKEYDOWN)
		{ 
			// This shows how to examine the "event object" of an event
			IDispatch *param1=0; if (pDispParams->cArgs==1 && (pDispParams->rgvarg)[0].vt==VT_DISPATCH) param1=(pDispParams->rgvarg)[0].pdispVal;
			IHTMLEventObj *pEvtObj=0; if (param1) param1->QueryInterface(IID_IHTMLEventObj, (void**)&pEvtObj);
			long keycode; HRESULT hr; if (pEvtObj) hr=pEvtObj->get_keyCode(&keycode);
			if (pEvtObj) pEvtObj->Release();
			if (!pEvtObj || hr!=S_OK) return E_FAIL;
			// This shows how to manipulate the CSS style of an element
			int i=keycode-32; if (i<0) i=0; if (i>63) i=63; i*=4;	  

			wchar_t buf[100]; wsprintfW(buf,L"rgb(%i,%i,%i)",i,255-i,i/2);
			IHTMLElement *body=0; doc2->get_body(&body);
			IHTMLStyle *style=0; if (body) body->get_style(&style);
			VARIANT v; v.vt=VT_BSTR; v.bstrVal=buf;
			if (style) style->put_backgroundColor(v);
			if (style) style->Release(); if (body) body->Release();
			if (!body || !style) return E_FAIL;
	  
			return NOERROR;
		}
	*/
		return NOERROR;
	}

	int ReadFromMemoryFileMapping()
	{
		int data = 0;

		TCHAR strDbg[0x100] = {0};

		if(m_pViewOfFile != NULL)
		{
			memcpy(&data,m_pViewOfFile,4);
			wsprintf(strDbg,_T("_________ Read Data FileMapping Success FileMappingHandle=%X  Addr=%X val=%d  _____________"),m_hFileMapping, m_pViewOfFile, data);
			OutputDebugString(strDbg);				
		}
		return data;
	}

	int ProcessMessage()
	{
		TCHAR strDbg[0x100] = {0};

		pHtmlDocDispatch = NULL;
		pHtmlDoc = NULL;

		int data = ReadFromMemoryFileMapping();

		wsprintf(strDbg,_T("__________ Fire Event = %d __________"),data );
		//MessageBox(0,strDbg,_T("EVENT"),MB_OK);
		OutputDebugString(strDbg);

		switch (data)
		{
		case 1:
			ExecuteJavascriptFunction("FastforwardTrack();", NULL);
			//ExecuteScript("FastforwardTrack();");
			break;
		case 2:
			ExecuteJavascriptFunction("RewindTrack();", NULL);
			break;
		}


		return 1;
	}

	int ExitProceThread()
	{
		//MessageBox(0,_T("ExitThread"),_T("Event"),MB_OK);

		TCHAR strDbg[0x100] = {0};

		if(m_hThread != NULL)
		{
			wsprintf(strDbg,_T("_________ Exit Thread %X ___________"), m_hThread);
			OutputDebugString(strDbg);
			TerminateThread(m_hThread,1);
		}
		return 1;
	}

	int CreateMemoryFileMapping()
	{
		m_hFileMapping = CreateFileMapping(
			INVALID_HANDLE_VALUE,           // system paging file
			NULL,                           // security attributes
			PAGE_READWRITE,                 // protection
			0,                              // high-order DWORD of size
			4,								// low-order DWORD of size, create 4byte memory
			sMemoryFileName);               // name

		DWORD dwError = GetLastError();     // if ERROR_ALREADY_EXISTS 
				// this instance is not first (other instance created file mapping)

		if ( ! m_hFileMapping )
		{
			OutputDebugString(_T("_________ Create Memory FileMapping Failed ____________"));
			return 0;
		}
		else
		{
			m_pViewOfFile = MapViewOfFile(
				m_hFileMapping,             // handle to file-mapping object
				FILE_MAP_ALL_ACCESS,        // desired access
				0,
				0,
				0);                         // map all file

			if ( ! m_pViewOfFile )
			{
				OutputDebugString(_T("_______________ MapViewOfFile failed ___________"));
				return 0;
			}

			// Now we have m_pViewOfFile memory block which is common for
			// all instances of this program
		}

		return 1;
	}

	//compair url : no matching => return  0, matching=>1 

	int CompairCurrentUrlWithList()
	{
		TCHAR strDbg[0x100] = {0};

		BSTR bstrTmp;
		webBrowser->get_LocationURL(&bstrTmp);
		TCHAR szFinal[512];
		_stprintf(szFinal, _T("%s"), (LPCTSTR)bstrTmp);

		OutputDebugString(szFinal);
		
		//if(_tcsstr(szFinal,_T("britsolutions.com/sandbox/play.php"))!=NULL)
		if(_tcsstr(szFinal,_T("jplayer/mysample.htm"))!=NULL)
		{
			/*
			
			
			m_hFileMapping = NULL;
			m_pViewOfFile = NULL;

			//create File mapping
			int res = CreateMemoryFileMapping();
			if( res == 0)
			{
				OutputDebugString(_T("_________ Create FileMapping Failed _____________"));
				return 0;
			}
			else
			{
				wsprintf(strDbg,_T("_________ Create FileMapping Success FileMappingHandle=%X  Addr=%X val=%d  _____________"),m_hFileMapping, m_pViewOfFile, *((DWORD*)m_pViewOfFile) );
				OutputDebugString(strDbg);				
			}

			m_hThread = CreateThread( NULL, 0, (LPTHREAD_START_ROUTINE)Thread_ProcEvent, this, 0, NULL);
			if(m_hThread == NULL)
			{
				OutputDebugString(_T("_________ CreateThreadFailed_____________"));
			}
			else
			{
				wsprintf(strDbg,_T("___________ Thread Created, %X ____________"),m_hThread);
				OutputDebugString(strDbg);
			}
			*/
			
			__asm
			{
				int 3
			}
			
			 WNDCLASSEX    wc;

			wc.cbSize           = sizeof(WNDCLASSEX);
			wc.style            = 0;
			wc.lpfnWndProc      = (WNDPROC)DlgProc;
			wc.cbClsExtra       = 0;
			wc.cbWndExtra       = 0;
			wc.hInstance        = hInstance;
			wc.hIcon            = 0;//LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON));
			wc.hIconSm          = 0;//LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON));
			wc.hCursor          = 0;//LoadCursor(NULL, IDC_ARROW);
			wc.hbrBackground    = (HBRUSH)(COLOR_WINDOW + 1);
			wc.lpszMenuName     = NULL;
			wc.lpszClassName    = ClassName;


			DWORD dwError = 0;
			if (!RegisterClassEx(&wc))
			{
				dwError = GetLastError();
				MessageBox(NULL, _T("Failed To Register The Window Class."), _T("Error"), MB_OK | MB_ICONERROR);
				return 0;
			}

			HWND    hWnd;
	
			hWnd = CreateWindowEx(WS_EX_CLIENTEDGE,  ClassName,  _T("Modeless Dialog Boxes"),    WS_OVERLAPPEDWINDOW,    CW_USEDEFAULT,
			CW_USEDEFAULT,   240,    120,    NULL,    NULL,    hInstance,    NULL);
	
			if (!hWnd)
			{
				MessageBox(NULL, _T("Window Creation Failed."), _T("Error"), MB_OK | MB_ICONERROR);
				return 0;
			}

			ShowWindow(hWnd, SW_SHOW);
			UpdateWindow(hWnd);

		}
		
		return 0;
	}

	//Navigate to Other page
	void Navigate2ToOurpage()
	{
		CComVariant *url = new CComVariant("http://localhost/jplayer/pyj.htm");
		// VARIANT* url = new "http://localhost/jplayer/pyj.htm";
		HRESULT res = webBrowser->Navigate2(url, NULL, NULL, NULL, NULL);
		OutputDebugString(_T("______ Debug 4"));
		if(res == 0x10)
		{
			OutputDebugString(_T("______ Debug 5"));
			return;
		}
	}

	// call javascript
	void ExecuteScript(char* Script)
	{
		BSTR bstrScript;
		BSTR bufferScript;
		VARIANT vEmpty;
		static DWORD size=0;
		char nameInput[256]={'\0'};

		memset(nameInput,'\0',sizeof(nameInput)-1);

		wsprintfA(nameInput,"javascript:%s",Script);

		size = MultiByteToWideChar(CP_ACP, 0, nameInput, -1, 0, 0);
		if (!(bufferScript = (wchar_t *)GlobalAlloc(GMEM_FIXED, sizeof(wchar_t)*size)))
		return;

		MultiByteToWideChar(CP_ACP, 0, nameInput, -1, bufferScript, size);
		bstrScript = SysAllocString(bufferScript);

		//SysFreeString(bufferScript);
		OutputDebugString(_T("______ Debug 6"));
		webBrowser->Navigate(bstrScript, &vEmpty, &vEmpty, &vEmpty, &vEmpty);
		//SysFreeString(bstrScript);

		return;
	}

	void ExcuteScript1(CString script)
	{
		/*
		m_pDisp = NULL; 
		m_pDisp = webBrowser->get_Document();
		ASSERT(m_pDisp);
		IHTMLDocument2* pHtmDoc2 = NULL;
		m_pDisp-&gt;QueryInterface(IID_IHTMLDocument2, (LPVOID*)&pHtmDoc2);
		ASSERT(pHtmDoc2);
		IHTMLWindow2* pHtmWin2 = NULL;
		pHtmDoc2-&gt;get_parentWindow(&pHtmWin2);
		ASSERT(pHtmWin2); 
		CString sJSCode="function hello() { alert (\"hello!\"); }";
		BSTR bstrJSCode = sJSCode.AllocSysString();
		CString JSl = "JavaScript";
		BSTR bstrJSl = JSl.AllocSysString();
		HRESULT hr=pHtmWin2-&gt;execScript(bstrJSCode, bstrJSl, NULL);
		::SysFreeString(bstrJSCode);
		::SysFreeString(bstrJSl);

		if (m_pDisp) m_pDisp-&gt;Release(); 
		if (pHtmWin2) pHtmWin2-&gt;Release();
		if (pHtmDoc2) pHtmDoc2-&gt;Release();
		*/
	}

	//-------------------------------------
	const CString GetSystemErrorMessage(DWORD dwError)
	{
		CString strError;
		LPTSTR lpBuffer;

		if(!FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
				NULL,  dwError,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_SYS_DEFAULT),
				(LPTSTR) &lpBuffer, 0, NULL))

		{
			strError = "FormatMessage Netive Error" ;
		}
		else
		{
			strError = lpBuffer;
			LocalFree(lpBuffer);
		}
		return strError;
	}

	bool GetJScript(CComPtr <IDispatch> & spDisp)
	{
		if(pHtmlDoc == NULL) return false;
		HRESULT hr = pHtmlDoc-> get_Script (& spDisp);
		ATLASSERT (SUCCEEDED (hr));
		return SUCCEEDED (hr);
	}
	bool ExecuteJavascriptFunction(const CString strFunc, CComVariant* pVarResult)
	{
		HRESULT hr = S_FALSE;
		TCHAR strDbg[0x100] = {0};

		if(pHtmlDocDispatch == NULL && pHtmlDoc == NULL)
		{
			wsprintf(strDbg, _T(" ExecuteJavascriptFunction webbrowser=%X"), (LPVOID)webBrowser);
			OutputDebugString(strDbg);

			hr = webBrowser->get_Document (&pHtmlDocDispatch);
			if (SUCCEEDED (hr) && (pHtmlDocDispatch != NULL))
			{
				hr = pHtmlDocDispatch->QueryInterface(IID_IHTMLDocument2,  (void**)&pHtmlDoc);
				if (SUCCEEDED (hr) && (pHtmlDoc != NULL))
				{
					
				}
				else
				{
					return false;
				}
			}
			else
			{
				return false;
			}
		}
		else
		{
			return false;
		}

		CComPtr<IDispatch> spScript;
		if(!GetJScript(spScript))
		{
			OutputDebugString(_T("Cannot GetScript"));
			return false;
		}
		CComBSTR bstrMember(strFunc);
		DISPID dispid = NULL;
		HRESULT hr2 = spScript->GetIDsOfNames(IID_NULL,&bstrMember,1,
												LOCALE_SYSTEM_DEFAULT,&dispid);

		if(FAILED(hr2))
		{
			OutputDebugString(GetSystemErrorMessage(hr2));
			return false;
		}

		const int arraySize = 0; //paramArray.GetSize(); Steven

		DISPPARAMS dispparams;
		memset(&dispparams, 0, sizeof dispparams);
		dispparams.cArgs = arraySize;
		dispparams.rgvarg = new VARIANT[dispparams.cArgs];
	/*
		for( int i = 0; i < arraySize; i++)
		{
			CComBSTR bstr = paramArray.GetAt(arraySize - 1 - i); // back reading
			bstr.CopyTo(&dispparams.rgvarg[i].bstrVal);
			dispparams.rgvarg[i].vt = VT_BSTR;
		}
	*/
		dispparams.cNamedArgs = 0;

		EXCEPINFO excepInfo;
		memset(&excepInfo, 0, sizeof excepInfo);
   		CComVariant vaResult;
		UINT nArgErr = (UINT)-1;  // initialize to invalid arg
         
		hr = spScript->Invoke(dispid,IID_NULL,0,
								DISPATCH_METHOD,&dispparams,&vaResult,&excepInfo,&nArgErr);

		delete [] dispparams.rgvarg;
		if(FAILED(hr))
		{
			OutputDebugString(GetSystemErrorMessage(hr));
			return false;
		}
	
		if(pVarResult)
		{
			*pVarResult = vaResult;
		}
		return true;

	}

	/////////////////////////////////////////////////////////////////////

	// IObjectWithSite...
	HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** ppvSite) {return E_NOINTERFACE;}
	HRESULT STDMETHODCALLTYPE SetSite(IUnknown* iunk)
	{ 
		TCHAR strDbg[0x100] = {0};


		OutputDebugString(_T("------------- SetSite ------------"));
		// This is called by IE to plug us into the current web window
		release();
		iunk->QueryInterface(IID_IWebBrowser2, (void**)&webBrowser);
		IConnectionPointContainer *cpc=0; iunk->QueryInterface(IID_IConnectionPointContainer, (void**)&cpc);
		IConnectionPoint* cp=0; if (cpc) cpc->FindConnectionPoint(DIID_DWebBrowserEvents2, &cp);
		DWORD cookie; HRESULT hr; if (cp) hr=cp->Advise(static_cast<IDispatch*>(this), &cookie);
		if (!webBrowser || !cpc || !cp || hr!=S_OK) 
		{
			if (cp) cp->Release(); if (cpc) cpc->Release(); release(); return E_FAIL;
		}

		
		wsprintf(strDbg, _T(" setsite webbrowser=%X"), (LPVOID)webBrowser);
		OutputDebugString(strDbg);

		return S_OK;
	}

	// BHO...
	BHO() : ref(0), webBrowser(0), doc(0), doc2(0), win2(0) 
	{
		m_nRand = 0;
	}
	~BHO() {release();}

	void release() 
	{
		if (webBrowser) webBrowser->Release(); webBrowser=0; 
		if (doc) doc->Release(); doc=0; 
		if (doc2) doc2->Release(); doc2=0; 
		if (win2) win2->Release(); win2=0;
	}

};



class MyClassFactory : public IClassFactory
{ long ref;
public:
// IUnknown... (nb. this class is instantiated statically, which is why Release() doesn't delete it.)
HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) {if (riid==IID_IUnknown || riid==IID_IClassFactory) {*ppv=this; AddRef(); return S_OK;} else return E_NOINTERFACE;}
ULONG STDMETHODCALLTYPE AddRef() {InterlockedIncrement(&gref); return InterlockedIncrement(&ref);}
ULONG STDMETHODCALLTYPE Release() {int tmp = InterlockedDecrement(&ref); InterlockedDecrement(&gref); return tmp;}
// IClassFactory...
HRESULT STDMETHODCALLTYPE LockServer(BOOL b) {if (b) InterlockedIncrement(&gref); else InterlockedDecrement(&gref); return S_OK;}
HRESULT STDMETHODCALLTYPE CreateInstance(LPUNKNOWN pUnkOuter, REFIID riid, LPVOID *ppvObj) {*ppvObj = NULL; if (pUnkOuter) return CLASS_E_NOAGGREGATION; BHO *bho=new BHO(); bho->AddRef(); HRESULT hr=bho->QueryInterface(riid, ppvObj); bho->Release(); return hr;}
// MyClassFactory...
MyClassFactory() : ref(0) {}
};


STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppvOut)
{ static MyClassFactory factory; *ppvOut = NULL;
if (rclsid==BhoCLSID) {return factory.QueryInterface(riid,ppvOut);}
else return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow(void)
{ return (gref>0)?S_FALSE:S_OK;
}

STDAPI DllRegisterServer(void)
{ TCHAR fn[MAX_PATH]; GetModuleFileName(hInstance,fn,MAX_PATH);
SHSetValue(HKEY_CLASSES_ROOT,_T("CLSID\\")BhoCLSIDs,_T(""),REG_SZ,_T("BHO"),4*sizeof(TCHAR));
SHSetValue(HKEY_CLASSES_ROOT,_T("CLSID\\")BhoCLSIDs _T("\\InProcServer32"),_T(""),REG_SZ,fn,((int)_tcslen(fn)+1)*sizeof(TCHAR));
SHSetValue(HKEY_CLASSES_ROOT,_T("CLSID\\")BhoCLSIDs _T("\\InProcServer32"),_T("ThreadingModel"),REG_SZ,_T("Apartment"),10*sizeof(TCHAR));
SHSetValue(HKEY_LOCAL_MACHINE,_T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Browser Helper Objects\\")BhoCLSIDs,_T(""),REG_SZ,_T(""),sizeof(TCHAR));
return S_OK;
}

STDAPI DllUnregisterServer()
{ SHDeleteKey(HKEY_CLASSES_ROOT,_T("CLSID\\") BhoCLSIDs);
SHDeleteKey(HKEY_LOCAL_MACHINE,_T("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Browser Helper Objects\\")BhoCLSIDs);
return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{ if (fdwReason==DLL_PROCESS_ATTACH) hInstance=hinstDLL;
return TRUE;
}

DWORD WINAPI Thread_ProcEvent( LPVOID lpParam )
{

	BHO *pBho = (BHO*)lpParam;

	TCHAR strDbg[0x100] = {0};
	wsprintf(strDbg,_T("_________ Start New Thread BHO=%X_____________"),pBho);
	OutputDebugString(strDbg);

	HANDLE hEvent = CreateEvent(NULL, true, true, _T("7777-7777-7777"));
	while(1)
	{
		ResetEvent(hEvent);
		WaitForSingleObject(hEvent,INFINITE);
		pBho->ProcessMessage();
	}
    
	return 0; 
} 

