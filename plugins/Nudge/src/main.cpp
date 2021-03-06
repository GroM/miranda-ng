#include "stdafx.h"

int nProtocol = 0;
static HANDLE hPopupClass;
HINSTANCE hInst;

HGENMENU g_hContactMenu;
OBJLIST<CNudgeElement> arNudges(5);
CNudgeElement DefaultNudge;
CShake shake;
CNudge GlobalNudge;

CLIST_INTERFACE *pcli;
int hLangpack = 0;

//========================
//  MirandaPluginInfo
//========================
PLUGININFOEX pluginInfo = {
	sizeof(PLUGININFOEX),
	__PLUGIN_NAME,
	PLUGIN_MAKE_VERSION(__MAJOR_VERSION, __MINOR_VERSION, __RELEASE_NUM, __BUILD_NUM),
	__DESCRIPTION,
	__AUTHOR,
	__AUTHOREMAIL,
	__COPYRIGHT,
	__AUTHORWEB,
	UNICODE_AWARE,
	// {E47CC215-0D28-462D-A0F6-3AE4443D2926}
	{ 0xe47cc215, 0xd28, 0x462d, { 0xa0, 0xf6, 0x3a, 0xe4, 0x44, 0x3d, 0x29, 0x26 } }
};

INT_PTR NudgeShowMenu(WPARAM wParam, LPARAM lParam)
{
	bool bEnabled = false;
	for (int i = 0; i < arNudges.getCount(); i++) {
		CNudgeElement &p = arNudges[i];
		if (!mir_strcmp((char*)wParam, p.ProtocolName)) {
			bEnabled = (GlobalNudge.useByProtocol) ? p.enabled : DefaultNudge.enabled;
			break;
		}
	}

	Menu_ShowItem(g_hContactMenu, bEnabled && lParam != 0);
	return 0;
}

INT_PTR NudgeSend(WPARAM hContact, LPARAM lParam)
{
	char *protoName = GetContactProto(hContact);
	int diff = time(NULL) - db_get_dw(hContact, "Nudge", "LastSent", time(NULL) - 30);
	if (diff < GlobalNudge.sendTimeSec) {
		wchar_t msg[500];
		mir_snwprintf(msg, TranslateT("You are not allowed to send too much nudge (only 1 each %d sec, %d sec left)"), GlobalNudge.sendTimeSec, 30 - diff);
		if (GlobalNudge.useByProtocol) {
			for (int i = 0; i < arNudges.getCount(); i++) {
				CNudgeElement &p = arNudges[i];
				if (!mir_strcmp(protoName, p.ProtocolName))
					Nudge_ShowPopup(&p, hContact, msg);
			}
		}
		else Nudge_ShowPopup(&DefaultNudge, hContact, msg);

		return 0;
	}

	db_set_dw(hContact, "Nudge", "LastSent", time(NULL));

	if (GlobalNudge.useByProtocol) {
		for (int i = 0; i < arNudges.getCount(); i++) {
			CNudgeElement &p = arNudges[i];
			if (!mir_strcmp(protoName, p.ProtocolName))
				if (p.showStatus)
					Nudge_SentStatus(&p, hContact);
		}
	}
	else if (DefaultNudge.showStatus)
		Nudge_SentStatus(&DefaultNudge, hContact);

	CallProtoService(protoName, PS_SEND_NUDGE, hContact, lParam);
	return 0;
}

void OpenContactList()
{
	HWND hWnd = pcli->hwndContactList;
	ShowWindow(hWnd, SW_RESTORE);
	ShowWindow(hWnd, SW_SHOW);
}

int NudgeReceived(WPARAM hContact, LPARAM lParam)
{
	char *protoName = GetContactProto(hContact);

	DWORD currentTimestamp = time(NULL);
	DWORD nudgeSentTimestamp = lParam ? (DWORD)lParam : currentTimestamp;

	int diff = currentTimestamp - db_get_dw(hContact, "Nudge", "LastReceived", currentTimestamp - 30);
	int diff2 = nudgeSentTimestamp - db_get_dw(hContact, "Nudge", "LastReceived2", nudgeSentTimestamp - 30);

	if (diff >= GlobalNudge.recvTimeSec)
		db_set_dw(hContact, "Nudge", "LastReceived", currentTimestamp);
	if (diff2 >= GlobalNudge.recvTimeSec)
		db_set_dw(hContact, "Nudge", "LastReceived2", nudgeSentTimestamp);

	if (GlobalNudge.useByProtocol) {
		for (int i = 0; i < arNudges.getCount(); i++) {
			CNudgeElement &p = arNudges[i];
			if (!mir_strcmp(protoName, p.ProtocolName)) {

				if (p.enabled) {
					if (p.useIgnoreSettings && CallService(MS_IGNORE_ISIGNORED, hContact, IGNOREEVENT_USERONLINE))
						return 0;

					DWORD Status = CallProtoService(protoName, PS_GETSTATUS, 0, 0);

					if (((p.statusFlags & NUDGE_ACC_ST0) && (Status <= ID_STATUS_OFFLINE)) ||
						((p.statusFlags & NUDGE_ACC_ST1) && (Status == ID_STATUS_ONLINE)) ||
						((p.statusFlags & NUDGE_ACC_ST2) && (Status == ID_STATUS_AWAY)) ||
						((p.statusFlags & NUDGE_ACC_ST3) && (Status == ID_STATUS_DND)) ||
						((p.statusFlags & NUDGE_ACC_ST4) && (Status == ID_STATUS_NA)) ||
						((p.statusFlags & NUDGE_ACC_ST5) && (Status == ID_STATUS_OCCUPIED)) ||
						((p.statusFlags & NUDGE_ACC_ST6) && (Status == ID_STATUS_FREECHAT)) ||
						((p.statusFlags & NUDGE_ACC_ST7) && (Status == ID_STATUS_INVISIBLE)) ||
						((p.statusFlags & NUDGE_ACC_ST8) && (Status == ID_STATUS_ONTHEPHONE)) ||
						((p.statusFlags & NUDGE_ACC_ST9) && (Status == ID_STATUS_OUTTOLUNCH)))
					{
						if (diff >= GlobalNudge.recvTimeSec) {
							if (p.showPopup)
								Nudge_ShowPopup(&p, hContact, p.recText);
							if (p.openContactList)
								OpenContactList();
							if (p.shakeClist)
								ShakeClist(hContact, lParam);
							if (p.openMessageWindow)
								CallService(MS_MSG_SENDMESSAGEW, hContact, 0);
							if (p.shakeChat)
								ShakeChat(hContact, lParam);
							if (p.autoResend)
								mir_forkthread(AutoResendNudge, (void*)hContact);

							SkinPlaySound(p.NudgeSoundname);
						}
					}

					if (diff2 >= GlobalNudge.recvTimeSec)
						if (p.showStatus)
							Nudge_ShowStatus(&p, hContact, nudgeSentTimestamp);
				}
				break;
			}
		}
	}
	else {
		if (DefaultNudge.enabled) {
			if (DefaultNudge.useIgnoreSettings && CallService(MS_IGNORE_ISIGNORED, hContact, IGNOREEVENT_USERONLINE))
				return 0;

			DWORD Status = CallService(MS_CLIST_GETSTATUSMODE, 0, 0);
			if (((DefaultNudge.statusFlags & NUDGE_ACC_ST0) && (Status <= ID_STATUS_OFFLINE)) ||
				((DefaultNudge.statusFlags & NUDGE_ACC_ST1) && (Status == ID_STATUS_ONLINE)) ||
				((DefaultNudge.statusFlags & NUDGE_ACC_ST2) && (Status == ID_STATUS_AWAY)) ||
				((DefaultNudge.statusFlags & NUDGE_ACC_ST3) && (Status == ID_STATUS_DND)) ||
				((DefaultNudge.statusFlags & NUDGE_ACC_ST4) && (Status == ID_STATUS_NA)) ||
				((DefaultNudge.statusFlags & NUDGE_ACC_ST5) && (Status == ID_STATUS_OCCUPIED)) ||
				((DefaultNudge.statusFlags & NUDGE_ACC_ST6) && (Status == ID_STATUS_FREECHAT)) ||
				((DefaultNudge.statusFlags & NUDGE_ACC_ST7) && (Status == ID_STATUS_INVISIBLE)) ||
				((DefaultNudge.statusFlags & NUDGE_ACC_ST8) && (Status == ID_STATUS_ONTHEPHONE)) ||
				((DefaultNudge.statusFlags & NUDGE_ACC_ST9) && (Status == ID_STATUS_OUTTOLUNCH)))
			{
				if (diff >= GlobalNudge.recvTimeSec) {
					if (DefaultNudge.showPopup)
						Nudge_ShowPopup(&DefaultNudge, hContact, DefaultNudge.recText);
					if (DefaultNudge.openContactList)
						OpenContactList();
					if (DefaultNudge.shakeClist)
						ShakeClist(hContact, lParam);
					if (DefaultNudge.openMessageWindow)
						CallService(MS_MSG_SENDMESSAGEW, hContact, 0);
					if (DefaultNudge.shakeChat)
						ShakeChat(hContact, lParam);
					if (DefaultNudge.autoResend)
						mir_forkthread(AutoResendNudge, (void*)hContact);

					SkinPlaySound(DefaultNudge.NudgeSoundname);
				}
			}

			if (diff2 >= GlobalNudge.recvTimeSec)
				if (DefaultNudge.showStatus)
					Nudge_ShowStatus(&DefaultNudge, hContact, nudgeSentTimestamp);
		}
	}
	return 0;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD, LPVOID)
{
	hInst = hinstDLL;
	return TRUE;
}

extern "C" __declspec(dllexport) PLUGININFOEX* MirandaPluginInfoEx(DWORD)
{
	return &pluginInfo;
}

void LoadProtocols(void)
{
	//Load the default nudge
	mir_snprintf(DefaultNudge.ProtocolName, "Default");
	mir_snprintf(DefaultNudge.NudgeSoundname, "Nudge : Default");
	SkinAddNewSoundEx(DefaultNudge.NudgeSoundname, LPGEN("Nudge"), LPGEN("Default Nudge"));
	DefaultNudge.Load();

	GlobalNudge.Load();

	int numberOfProtocols = 0;
	PROTOACCOUNT **ppProtocolDescriptors;
	Proto_EnumAccounts(&numberOfProtocols, &ppProtocolDescriptors);
	for (int i = 0; i < numberOfProtocols; i++)
		Nudge_AddAccount(ppProtocolDescriptors[i]);

	shake.Load();
}

static IconItem iconList[] =
{
	{ LPGEN("Nudge as Default"), "Nudge_Default", IDI_NUDGE }
};

// Load icons
void LoadIcons(void)
{
	Icon_Register(hInst, LPGEN("Nudge"), iconList, _countof(iconList));
}

// Nudge support
static int TabsrmmButtonPressed(WPARAM wParam, LPARAM lParam)
{
	CustomButtonClickData *cbcd = (CustomButtonClickData *)lParam;

	if (!mir_strcmp(cbcd->pszModule, "Nudge"))
		NudgeSend(wParam, 0);

	return 0;
}

static int TabsrmmButtonInit(WPARAM, LPARAM)
{
	BBButton bbd = {};
	bbd.pszModuleName = "Nudge";
	bbd.pwszTooltip = LPGENW("Send Nudge");
	bbd.dwDefPos = 300;
	bbd.bbbFlags = BBBF_ISIMBUTTON | BBBF_CANBEHIDDEN;
	bbd.hIcon = iconList[0].hIcolib;
	bbd.dwButtonID = 6000;
	Srmm_AddButton(&bbd);

	return 0;
}

void HideNudgeButton(MCONTACT hContact)
{
	char *szProto = GetContactProto(hContact);
	if (!ProtoServiceExists(szProto, PS_SEND_NUDGE)) {
		BBButton bbd = {};
		bbd.pszModuleName = "Nudge";
		bbd.dwButtonID = 6000;
		bbd.bbbFlags = BBSF_HIDDEN | BBSF_DISABLED;
		Srmm_SetButtonState(hContact, &bbd);
	}
}

static int ContactWindowOpen(WPARAM, LPARAM lParam)
{
	MessageWindowEventData *MWeventdata = (MessageWindowEventData*)lParam;
	if (MWeventdata->uType == MSG_WINDOW_EVT_OPENING && MWeventdata->hContact)
		HideNudgeButton(MWeventdata->hContact);

	return 0;
}

static int PrebuildContactMenu(WPARAM hContact, LPARAM)
{
	char *szProto = GetContactProto(hContact);
	if (szProto != NULL) {
		bool isChat = db_get_b(hContact, szProto, "ChatRoom", false) != 0;
		NudgeShowMenu((WPARAM)szProto, !isChat);
	}

	return 0;
}

int ModulesLoaded(WPARAM, LPARAM)
{
	LoadProtocols();
	LoadPopupClass();

	HookEvent(ME_CLIST_PREBUILDCONTACTMENU, PrebuildContactMenu);

	if (HookEvent(ME_MSG_TOOLBARLOADED, TabsrmmButtonInit)) {
		HookEvent(ME_MSG_BUTTONPRESSED, TabsrmmButtonPressed);
		HookEvent(ME_MSG_WINDOWEVENT, ContactWindowOpen);
	}
	return 0;
}

int AccListChanged(WPARAM wParam, LPARAM lParam)
{
	PROTOACCOUNT *proto = (PROTOACCOUNT*)wParam;
	if (proto == NULL)
		return 0;

	if (lParam == PRAC_ADDED)
		Nudge_AddAccount(proto);
	return 0;
}

extern "C" int __declspec(dllexport) Load(void)
{
	mir_getLP(&pluginInfo);
	pcli = Clist_GetInterface();

	LoadIcons();

	HookEvent(ME_SYSTEM_MODULESLOADED, ModulesLoaded);
	HookEvent(ME_PROTO_ACCLISTCHANGED, AccListChanged);
	HookEvent(ME_OPT_INITIALISE, NudgeOptInit);

	// Create function for plugins
	CreateServiceFunction(MS_SHAKE_CLIST, ShakeClist);
	CreateServiceFunction(MS_SHAKE_CHAT, ShakeChat);
	CreateServiceFunction(MS_NUDGE_SEND, NudgeSend);
	CreateServiceFunction(MS_NUDGE_SHOWMENU, NudgeShowMenu);

	// Add contact menu entry
	CMenuItem mi;
	SET_UID(mi, 0xd617db26, 0x22ba, 0x4205, 0x9c, 0x3e, 0x53, 0x10, 0xbc, 0xcf, 0xce, 0x19);
	mi.flags = CMIF_NOTOFFLINE | CMIF_UNICODE;
	mi.position = -500050004;
	mi.hIcolibItem = iconList[0].hIcolib;
	mi.name.w = LPGENW("Send &Nudge");
	mi.pszService = MS_NUDGE_SEND;
	g_hContactMenu = Menu_AddContactMenuItem(&mi);

	// register special type of event
	// there's no need to declare the special service for getting text
	// because a blob contains only text
	DBEVENTTYPEDESCR evtype = { sizeof(evtype) };
	evtype.module = MODULENAME;
	evtype.eventType = 1;
	evtype.descr = LPGEN("Nudge");
	evtype.eventIcon = iconList[0].hIcolib;
	evtype.flags = DETF_HISTORY | DETF_MSGWINDOW;
	DbEvent_RegisterType(&evtype);
	return 0;
}

extern "C" int __declspec(dllexport) Unload(void)
{
	arNudges.destroy();
	return 0;
}

LRESULT CALLBACK NudgePopupProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_COMMAND:
		CallService(MS_MSG_SENDMESSAGEW, (WPARAM)PUGetContact(hWnd), 0);
		PUDeletePopup(hWnd);
		break;

	case WM_CONTEXTMENU:
		PUDeletePopup(hWnd);
		break;
	case UM_FREEPLUGINDATA:
		//Here we'd free our own data, if we had it.
		return FALSE;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

static int OnShutdown(WPARAM, LPARAM)
{
	Popup_UnregisterClass(hPopupClass);
	return 0;
}

void LoadPopupClass()
{
	POPUPCLASS ppc = { sizeof(ppc) };
	ppc.flags = PCF_TCHAR;
	ppc.pszName = "Nudge";
	ppc.pwszDescription = LPGENW("Show Nudge");
	ppc.hIcon = IcoLib_GetIconByHandle(iconList[0].hIcolib);
	ppc.colorBack = NULL;
	ppc.colorText = NULL;
	ppc.iSeconds = 0;
	ppc.PluginWindowProc = NudgePopupProc;
	if (hPopupClass = Popup_RegisterClass(&ppc))
		HookEvent(ME_SYSTEM_SHUTDOWN, OnShutdown);
}

int Preview()
{
	MCONTACT hContact = db_find_first();
	if (GlobalNudge.useByProtocol) {
		for (int i = 0; i < arNudges.getCount(); i++) {
			CNudgeElement &p = arNudges[i];
			if (p.enabled) {
				SkinPlaySound(p.NudgeSoundname);
				if (p.showPopup)
					Nudge_ShowPopup(&p, hContact, p.recText);
				if (p.openContactList)
					OpenContactList();
				if (p.shakeClist)
					ShakeClist(0, 0);
				if (p.openMessageWindow)
					CallService(MS_MSG_SENDMESSAGEW, hContact, NULL);
				if (p.shakeChat)
					ShakeChat(hContact, (LPARAM)time(NULL));
			}
		}
	}
	else {
		if (DefaultNudge.enabled) {
			SkinPlaySound(DefaultNudge.NudgeSoundname);
			if (DefaultNudge.showPopup)
				Nudge_ShowPopup(&DefaultNudge, hContact, DefaultNudge.recText);
			if (DefaultNudge.openContactList)
				OpenContactList();
			if (DefaultNudge.shakeClist)
				ShakeClist(0, 0);
			if (DefaultNudge.openMessageWindow)
				CallService(MS_MSG_SENDMESSAGEW, hContact, NULL);
			if (DefaultNudge.shakeChat)
				ShakeChat(hContact, (LPARAM)time(NULL));
		}
	}
	return 0;
}

void Nudge_ShowPopup(CNudgeElement*, MCONTACT hContact, wchar_t * Message)
{
	hContact = db_mc_tryMeta(hContact);
	wchar_t *lpzContactName = (wchar_t*)pcli->pfnGetContactDisplayName(hContact, 0);

	if (ServiceExists(MS_POPUP_ADDPOPUPCLASS)) {
		POPUPDATACLASS NudgePopup = { 0 };
		NudgePopup.cbSize = sizeof(NudgePopup);
		NudgePopup.hContact = hContact;
		NudgePopup.pwszText = Message;
		NudgePopup.pwszTitle = lpzContactName;
		NudgePopup.pszClassName = "nudge";
		CallService(MS_POPUP_ADDPOPUPCLASS, 0, (LPARAM)&NudgePopup);
	}
	else if (ServiceExists(MS_POPUP_ADDPOPUPT)) {
		POPUPDATAT NudgePopup = { 0 };
		NudgePopup.lchContact = hContact;
		NudgePopup.lchIcon = IcoLib_GetIconByHandle(iconList[0].hIcolib);
		NudgePopup.colorBack = 0;
		NudgePopup.colorText = 0;
		NudgePopup.iSeconds = 0;
		NudgePopup.PluginWindowProc = NudgePopupProc;
		NudgePopup.PluginData = (void *)1;

		wcscpy_s(NudgePopup.lptzText, Message);
		wcscpy_s(NudgePopup.lptzContactName, lpzContactName);

		CallService(MS_POPUP_ADDPOPUPT, (WPARAM)&NudgePopup, 0);
	}
	else MessageBox(NULL, Message, lpzContactName, 0);
}

void Nudge_SentStatus(CNudgeElement *n, MCONTACT hContact)
{
	T2Utf buff(n->senText);

	DBEVENTINFO dbei = {};
	dbei.szModule = MODULENAME;
	dbei.flags = DBEF_SENT | DBEF_UTF;
	dbei.timestamp = (DWORD)time(NULL);
	dbei.eventType = 1;
	dbei.cbBlob = (DWORD)mir_strlen(buff) + 1;
	dbei.pBlob = (PBYTE)buff;
	db_event_add(hContact, &dbei);
}

void Nudge_ShowStatus(CNudgeElement *n, MCONTACT hContact, DWORD timestamp)
{
	T2Utf buff(n->recText);

	DBEVENTINFO dbei = {};
	dbei.szModule = MODULENAME;
	dbei.eventType = 1;
	dbei.flags = DBEF_UTF;
	dbei.timestamp = timestamp;
	dbei.cbBlob = (DWORD)mir_strlen(buff) + 1;
	dbei.pBlob = (PBYTE)buff;
	db_event_add(hContact, &dbei);
}

void Nudge_AddAccount(PROTOACCOUNT *proto)
{
	char str[MAXMODULELABELLENGTH + 10];
	mir_snprintf(str, "%s/Nudge", proto->szModuleName);
	HANDLE hevent = HookEvent(str, NudgeReceived);
	if (hevent == NULL)
		return;

	nProtocol++;

	// Add a specific sound per protocol
	CNudgeElement *p = new CNudgeElement();
	mir_snprintf(p->NudgeSoundname, "%s: Nudge", proto->szModuleName);

	strcpy_s(p->ProtocolName, proto->szModuleName);
	wcscpy_s(p->AccountName, proto->tszAccountName);

	p->Load();
	p->hEvent = hevent;

	wchar_t soundDesc[MAXMODULELABELLENGTH + 10];
	mir_snwprintf(soundDesc, LPGENW("Nudge for %s"), proto->tszAccountName);
	SkinAddNewSoundExW(p->NudgeSoundname, LPGENW("Nudge"), soundDesc);

	arNudges.insert(p);
}

void AutoResendNudge(void *wParam)
{
	Sleep(GlobalNudge.resendDelaySec * 1000);
	NudgeSend((WPARAM)wParam, NULL);
}
