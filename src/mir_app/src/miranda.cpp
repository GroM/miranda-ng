/*

Miranda NG: the free IM client for Microsoft* Windows*

Copyright (�) 2012-16 Miranda NG project (http://miranda-ng.org),
Copyright (c) 2000-12 Miranda IM project,
all portions of this codebase are copyrighted to the people
listed in contributors.txt.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "stdafx.h"

#pragma comment(lib, "version.lib")

int LoadDefaultModules(void);
void UnloadNewPluginsModule(void);
void UnloadDefaultModules(void);

pfnDrawThemeTextEx drawThemeTextEx;
pfnSetWindowThemeAttribute setWindowThemeAttribute;
pfnBufferedPaintInit bufferedPaintInit;
pfnBufferedPaintUninit bufferedPaintUninit;
pfnBeginBufferedPaint beginBufferedPaint;
pfnEndBufferedPaint endBufferedPaint;
pfnGetBufferedPaintBits getBufferedPaintBits;

pfnDwmExtendFrameIntoClientArea dwmExtendFrameIntoClientArea;
pfnDwmIsCompositionEnabled dwmIsCompositionEnabled;

ITaskbarList3 * pTaskbarInterface;

HANDLE hOkToExitEvent, hModulesLoadedEvent;
HANDLE hShutdownEvent, hPreShutdownEvent;
static HANDLE hWaitObjects[MAXIMUM_WAIT_OBJECTS-1];
static char *pszWaitServices[MAXIMUM_WAIT_OBJECTS-1];
static int waitObjectCount = 0;
HANDLE hMirandaShutdown;
HINSTANCE g_hInst;
DWORD hMainThreadId;
int hLangpack = 0;
bool bModulesLoadedFired = false;
int g_iIconX, g_iIconY, g_iIconSX, g_iIconSY;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD dwReason, LPVOID)
{
	if (dwReason == DLL_PROCESS_ATTACH) {
		g_hInst = hinstDLL;
		g_iIconX = GetSystemMetrics(SM_CXICON);
		g_iIconY = GetSystemMetrics(SM_CYICON);
		g_iIconSX = GetSystemMetrics(SM_CXSMICON);
		g_iIconSY = GetSystemMetrics(SM_CYSMICON);
	}
	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////////////////
// exception handling

static INT_PTR srvGetExceptionFilter(WPARAM, LPARAM)
{
	return (INT_PTR)GetExceptionFilter();
}

static INT_PTR srvSetExceptionFilter(WPARAM, LPARAM lParam)
{
	return (INT_PTR)SetExceptionFilter((pfnExceptionFilter)lParam);
}

/////////////////////////////////////////////////////////////////////////////////////////

typedef LONG(WINAPI *pNtQIT)(HANDLE, LONG, PVOID, ULONG, PULONG);
#define ThreadQuerySetWin32StartAddress 9

INT_PTR MirandaIsTerminated(WPARAM, LPARAM)
{
	return WaitForSingleObject(hMirandaShutdown, 0) == WAIT_OBJECT_0;
}

static void __cdecl compactHeapsThread(void*)
{
	Thread_SetName("compactHeapsThread");

	while (!Miranda_Terminated()) {
		HANDLE hHeaps[256];
		DWORD hc;
		SleepEx((1000 * 60) * 5, TRUE); // every 5 minutes
		hc = GetProcessHeaps(255, (PHANDLE)&hHeaps);
		if (hc != 0 && hc < 256) {
			DWORD j;
			for (j = 0; j < hc; j++)
				HeapCompact(hHeaps[j], 0);
		}
	} //while
}

void(*SetIdleCallback) (void) = NULL;

static INT_PTR SystemSetIdleCallback(WPARAM, LPARAM lParam)
{
	if (lParam && SetIdleCallback == NULL) {
		SetIdleCallback = (void(*)(void))lParam;
		return 1;
	}
	return 0;
}

static DWORD dwEventTime = 0;
void checkIdle(MSG * msg)
{
	switch (msg->message) {
	case WM_MOUSEACTIVATE:
	case WM_MOUSEMOVE:
	case WM_CHAR:
		dwEventTime = GetTickCount();
	}
}

static INT_PTR SystemGetIdle(WPARAM, LPARAM lParam)
{
	if (lParam) *(DWORD*)lParam = dwEventTime;
	return 0;
}

static int SystemShutdownProc(WPARAM, LPARAM)
{
	UnloadDefaultModules();
	return 0;
}

#define MIRANDA_PROCESS_WAIT_TIMEOUT        60000
#define MIRANDA_PROCESS_WAIT_RESOLUTION     1000
#define MIRANDA_PROCESS_WAIT_STEPS          (MIRANDA_PROCESS_WAIT_TIMEOUT/MIRANDA_PROCESS_WAIT_RESOLUTION)

class CWaitRestartDlg : public CDlgBase
{
	HANDLE m_hProcess;
	CTimer m_timer;
	CProgress m_progress;
	CCtrlButton m_cancel;

protected:
	void OnInitDialog();
	
	void Timer_OnEvent(CTimer*);

	void Cancel_OnClick(CCtrlBase*);

public:
	CWaitRestartDlg(HANDLE hProcess);
};

CWaitRestartDlg::CWaitRestartDlg(HANDLE hProcess)
	: CDlgBase(g_hInst, IDD_WAITRESTART), m_timer(this, 1),
	m_progress(this, IDC_PROGRESSBAR), m_cancel(this, IDCANCEL)
{
	m_autoClose = 0;
	m_hProcess = hProcess;
	m_timer.OnEvent = Callback(this, &CWaitRestartDlg::Timer_OnEvent);
	m_cancel.OnClick = Callback(this, &CWaitRestartDlg::Cancel_OnClick);
}

void CWaitRestartDlg::OnInitDialog()
{
	m_progress.SetRange(MIRANDA_PROCESS_WAIT_STEPS);
	m_progress.SetStep(1);
	m_timer.Start(MIRANDA_PROCESS_WAIT_RESOLUTION);
}

void CWaitRestartDlg::Timer_OnEvent(CTimer*)
{
	if (m_progress.Move() == MIRANDA_PROCESS_WAIT_STEPS)
		EndModal(0);
	if (WaitForSingleObject(m_hProcess, 1) != WAIT_TIMEOUT) {
		m_progress.SetPosition(MIRANDA_PROCESS_WAIT_STEPS);
		EndModal(0);
	}
}

void CWaitRestartDlg::Cancel_OnClick(CCtrlBase*)
{
	m_progress.SetPosition(MIRANDA_PROCESS_WAIT_STEPS);
	EndModal(1);
}

INT_PTR CheckRestart()
{
	LPCTSTR tszPID = CmdLine_GetOption(L"restart");
	if (tszPID) {
		HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, _wtol(tszPID));
		if (hProcess) {
			INT_PTR result = CWaitRestartDlg(hProcess).DoModal();
			CloseHandle(hProcess);
			return result;
		}
	}
	return 0;
}

static void crtErrorHandler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t)
{}

int WINAPI mir_main(LPTSTR cmdLine)
{
	hMainThreadId = GetCurrentThreadId();

	_set_invalid_parameter_handler(&crtErrorHandler);
#ifdef _DEBUG
	_CrtSetReportMode(_CRT_ASSERT, 0);
#endif

	CmdLine_Parse(cmdLine);
	setlocale(LC_ALL, "");

#ifdef _DEBUG
	if (CmdLine_GetOption(L"memdebug"))
		_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	HMODULE hDwmApi, hThemeAPI;
	if (IsWinVerVistaPlus()) {
		hDwmApi = LoadLibrary(L"dwmapi.dll");
		if (hDwmApi) {
			dwmExtendFrameIntoClientArea = (pfnDwmExtendFrameIntoClientArea)GetProcAddress(hDwmApi, "DwmExtendFrameIntoClientArea");
			dwmIsCompositionEnabled = (pfnDwmIsCompositionEnabled)GetProcAddress(hDwmApi, "DwmIsCompositionEnabled");
		}
		hThemeAPI = LoadLibrary(L"uxtheme.dll");
		if (hThemeAPI) {
			drawThemeTextEx = (pfnDrawThemeTextEx)GetProcAddress(hThemeAPI, "DrawThemeTextEx");
			setWindowThemeAttribute = (pfnSetWindowThemeAttribute)GetProcAddress(hThemeAPI, "SetWindowThemeAttribute");
			bufferedPaintInit = (pfnBufferedPaintInit)GetProcAddress(hThemeAPI, "BufferedPaintInit");
			bufferedPaintUninit = (pfnBufferedPaintUninit)GetProcAddress(hThemeAPI, "BufferedPaintUninit");
			beginBufferedPaint = (pfnBeginBufferedPaint)GetProcAddress(hThemeAPI, "BeginBufferedPaint");
			endBufferedPaint = (pfnEndBufferedPaint)GetProcAddress(hThemeAPI, "EndBufferedPaint");
			getBufferedPaintBits = (pfnGetBufferedPaintBits)GetProcAddress(hThemeAPI, "GetBufferedPaintBits");
		}
	}
	else hDwmApi = hThemeAPI = 0;

	if (bufferedPaintInit)
		bufferedPaintInit();

	OleInitialize(NULL);

	if (IsWinVer7Plus())
		CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_ALL, IID_ITaskbarList3, (void**)&pTaskbarInterface);

	int result = 0;
	if (LoadDefaultModules()) {
		SetEvent(hMirandaShutdown);
		NotifyEventHooks(hPreShutdownEvent, 0, 0);
		NotifyEventHooks(hShutdownEvent, 0, 0);
		UnloadDefaultModules();

		result = 1;
	}
	else {
		InitPathVar();
		NotifyEventHooks(hModulesLoadedEvent, 0, 0);
		bModulesLoadedFired = true;

		// ensure that the kernel hooks the SystemShutdownProc() after all plugins
		HookEvent(ME_SYSTEM_SHUTDOWN, SystemShutdownProc);

		mir_forkthread(compactHeapsThread);
		CreateServiceFunction(MS_SYSTEM_SETIDLECALLBACK, SystemSetIdleCallback);
		CreateServiceFunction(MS_SYSTEM_GETIDLE, SystemGetIdle);
		dwEventTime = GetTickCount();
		DWORD myPid = GetCurrentProcessId();

		bool messageloop = true;
		while (messageloop) {
			MSG msg;
			BOOL dying = FALSE;
			DWORD rc = MsgWaitForMultipleObjectsEx(waitObjectCount, hWaitObjects, INFINITE, QS_ALLINPUT, MWMO_ALERTABLE);
			if (rc < WAIT_OBJECT_0 + waitObjectCount) {
				rc -= WAIT_OBJECT_0;
				CallService(pszWaitServices[rc], (WPARAM)hWaitObjects[rc], 0);
			}
			//
			while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
				if (msg.message != WM_QUIT) {
					HWND h = GetForegroundWindow();
					DWORD pid = 0;
					checkIdle(&msg);
					if (h != NULL && GetWindowThreadProcessId(h, &pid) && pid == myPid && GetClassLongPtr(h, GCW_ATOM) == 32770)
						if (h != NULL && IsDialogMessage(h, &msg)) /* Wine fix. */
							continue;

					TranslateMessage(&msg);
					DispatchMessage(&msg);
					if (SetIdleCallback != NULL)
						SetIdleCallback();
				}
				else if (!dying) {
					dying++;
					SetEvent(hMirandaShutdown);
					NotifyEventHooks(hPreShutdownEvent, 0, 0);

					// this spins and processes the msg loop, objects and APC.
					Thread_Wait();
					NotifyEventHooks(hShutdownEvent, 0, 0);
					// if the hooks generated any messages, it'll get processed before the second WM_QUIT
					PostQuitMessage(0);
				}
				else if (dying)
					messageloop = false;
			}
		}
	}

	UnloadNewPluginsModule();
	UnloadCoreModule();
	CloseHandle(hMirandaShutdown);
	FreeLibrary(hDwmApi);
	FreeLibrary(hThemeAPI);

	if (pTaskbarInterface)
		pTaskbarInterface->Release();

	OleUninitialize();

	if (bufferedPaintUninit)
		bufferedPaintUninit();
	return result;
}

static INT_PTR OkToExit(WPARAM, LPARAM)
{
	return NotifyEventHooks(hOkToExitEvent, 0, 0) == 0;
}

static INT_PTR GetMirandaVersion(WPARAM, LPARAM)
{
	wchar_t filename[MAX_PATH];
	GetModuleFileName(g_hInst, filename, _countof(filename));

	DWORD unused, verInfoSize = GetFileVersionInfoSize(filename, &unused);
	PVOID pVerInfo = _alloca(verInfoSize);
	GetFileVersionInfo(filename, 0, verInfoSize, pVerInfo);

	UINT blockSize;
	VS_FIXEDFILEINFO *vsffi;
	VerQueryValue(pVerInfo, L"\\", (PVOID*)&vsffi, &blockSize);
	DWORD ver = (((vsffi->dwProductVersionMS >> 16) & 0xFF) << 24) |
		((vsffi->dwProductVersionMS & 0xFF) << 16) |
		(((vsffi->dwProductVersionLS >> 16) & 0xFF) << 8) |
		(vsffi->dwProductVersionLS & 0xFF);
	return (INT_PTR)ver;
}

static INT_PTR GetMirandaFileVersion(WPARAM, LPARAM lParam)
{
	wchar_t filename[MAX_PATH];
	GetModuleFileName(g_hInst, filename, _countof(filename));

	DWORD unused, verInfoSize = GetFileVersionInfoSize(filename, &unused);
	PVOID pVerInfo = _alloca(verInfoSize);
	GetFileVersionInfo(filename, 0, verInfoSize, pVerInfo);

	UINT blockSize;
	VS_FIXEDFILEINFO *vsffi;
	VerQueryValue(pVerInfo, L"\\", (PVOID*)&vsffi, &blockSize);

	WORD* p = (WORD*)lParam;
	p[0] = HIWORD(vsffi->dwProductVersionMS);
	p[1] = LOWORD(vsffi->dwProductVersionMS);
	p[2] = HIWORD(vsffi->dwProductVersionLS);
	p[3] = LOWORD(vsffi->dwProductVersionLS);
	return 0;
}

static INT_PTR GetMirandaVersionText(WPARAM wParam, LPARAM lParam)
{
	wchar_t filename[MAX_PATH], *productVersion;
	GetModuleFileName(g_hInst, filename, _countof(filename));

	DWORD unused, verInfoSize = GetFileVersionInfoSize(filename, &unused);
	PVOID pVerInfo = _alloca(verInfoSize);
	GetFileVersionInfo(filename, 0, verInfoSize, pVerInfo);

	UINT blockSize;
	VerQueryValue(pVerInfo, L"\\StringFileInfo\\000004b0\\ProductVersion", (LPVOID*)&productVersion, &blockSize);
	strncpy((char*)lParam, _T2A(productVersion), wParam);
#if defined(_WIN64)
	strcat_s((char*)lParam, wParam, " x64");
#endif
	return 0;
}

INT_PTR WaitOnHandle(WPARAM wParam, LPARAM lParam)
{
	if (waitObjectCount >= MAXIMUM_WAIT_OBJECTS - 1)
		return 1;

	hWaitObjects[waitObjectCount] = (HANDLE)wParam;
	pszWaitServices[waitObjectCount] = (char*)lParam;
	waitObjectCount++;
	return 0;
}

static INT_PTR RemoveWait(WPARAM wParam, LPARAM)
{
	int i;

	for (i = 0; i < waitObjectCount; i++)
		if (hWaitObjects[i] == (HANDLE)wParam)
			break;

	if (i == waitObjectCount)
		return 1;

	waitObjectCount--;
	memmove(&hWaitObjects[i], &hWaitObjects[i + 1], sizeof(HANDLE)*(waitObjectCount - i));
	memmove(&pszWaitServices[i], &pszWaitServices[i + 1], sizeof(char*)*(waitObjectCount - i));
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

int LoadSystemModule(void)
{
	hMirandaShutdown = CreateEvent(NULL, TRUE, FALSE, NULL);

	hShutdownEvent = CreateHookableEvent(ME_SYSTEM_SHUTDOWN);
	hPreShutdownEvent = CreateHookableEvent(ME_SYSTEM_PRESHUTDOWN);
	hModulesLoadedEvent = CreateHookableEvent(ME_SYSTEM_MODULESLOADED);
	hOkToExitEvent = CreateHookableEvent(ME_SYSTEM_OKTOEXIT);

	CreateServiceFunction(MS_SYSTEM_TERMINATED, MirandaIsTerminated);
	CreateServiceFunction(MS_SYSTEM_OKTOEXIT, OkToExit);
	CreateServiceFunction(MS_SYSTEM_GETVERSION, GetMirandaVersion);
	CreateServiceFunction(MS_SYSTEM_GETFILEVERSION, GetMirandaFileVersion);
	CreateServiceFunction(MS_SYSTEM_GETVERSIONTEXT, GetMirandaVersionText);
	CreateServiceFunction(MS_SYSTEM_WAITONHANDLE, WaitOnHandle);
	CreateServiceFunction(MS_SYSTEM_REMOVEWAIT, RemoveWait);
	CreateServiceFunction(MS_SYSTEM_GETEXCEPTFILTER, srvGetExceptionFilter);
	CreateServiceFunction(MS_SYSTEM_SETEXCEPTFILTER, srvSetExceptionFilter);
	return 0;
}
