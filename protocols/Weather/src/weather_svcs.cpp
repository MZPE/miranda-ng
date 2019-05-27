/*
Weather Protocol plugin for Miranda IM
Copyright (c) 2012 Miranda NG team
Copyright (c) 2005-2011 Boris Krasnovskiy All Rights Reserved
Copyright (c) 2002-2005 Calvin Che

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; version 2
of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
This file contain the source related to weather protocol services
as required for a Miranda protocol. Also, it contains functions for
building/changing the weather menu items.
*/

#include "stdafx.h"

static HGENMENU hEnableDisablePopupMenu;
static HGENMENU hEnableDisableMenu;

//============  MIRANDA PROTOCOL SERVICES  ============

// protocol service function for setting weather protocol status
INT_PTR WeatherSetStatus(WPARAM new_status, LPARAM)
{
	new_status = new_status != ID_STATUS_OFFLINE ? ID_STATUS_ONLINE : ID_STATUS_OFFLINE;

	// if we don't want to show status for default station
	if (!opt.NoProtoCondition && status != new_status) {
		old_status = status;
		status = new_status != ID_STATUS_OFFLINE ? ID_STATUS_ONLINE : ID_STATUS_OFFLINE;
		ProtoBroadcastAck(MODULENAME, NULL, ACKTYPE_STATUS, ACKRESULT_SUCCESS, (HANDLE)old_status, status);

		UpdateMenu(new_status != ID_STATUS_OFFLINE);
		if (new_status != ID_STATUS_OFFLINE)
			UpdateAll(FALSE, FALSE);
	}

	return 0;
}

// get capabilities protocol service function
INT_PTR WeatherGetCaps(WPARAM wParam, LPARAM)
{
	INT_PTR ret = 0;

	switch (wParam) {
	case PFLAGNUM_1:
		// support search and visible list
		ret = PF1_BASICSEARCH | PF1_ADDSEARCHRES | PF1_EXTSEARCH | PF1_VISLIST | PF1_MODEMSGRECV;
		break;

	case PFLAGNUM_2:
		ret = PF2_ONLINE | PF2_INVISIBLE | PF2_SHORTAWAY | PF2_LONGAWAY | PF2_LIGHTDND | PF2_HEAVYDND;
		break;

	case PFLAGNUM_4:
		ret = PF4_AVATARS | PF4_NOCUSTOMAUTH | PF4_NOAUTHDENYREASON | PF4_FORCEAUTH;
		break;

	case PFLAGNUM_5: /* this is PFLAGNUM_5 change when alpha SDK is released */
		ret = PF2_INVISIBLE | PF2_SHORTAWAY | PF2_LONGAWAY | PF2_LIGHTDND | PF2_HEAVYDND;
		break;

	case PFLAG_UNIQUEIDTEXT:
		ret = (INT_PTR)Translate("Station ID");
		break;
	}
	return ret;
}

// protocol service function to get weather protocol name
INT_PTR WeatherGetName(WPARAM wParam, LPARAM lParam)
{
	strncpy((char*)lParam, WEATHERPROTOTEXT, wParam - 1);
	*((char*)lParam + wParam - 1) = 0;
	return 0;
}

// protocol service function to get the current status of the protocol
INT_PTR WeatherGetStatus(WPARAM, LPARAM)
{
	return status;
}

// protocol service function to get the icon of the protocol
INT_PTR WeatherLoadIcon(WPARAM wParam, LPARAM)
{
	return (LOWORD(wParam) == PLI_PROTOCOL) ? (INT_PTR)CopyIcon(g_plugin.getIcon(IDI_ICON)) : 0;
}

static void __cdecl AckThreadProc(HANDLE param)
{
	Sleep(100);
	ProtoBroadcastAck(MODULENAME, (DWORD_PTR)param, ACKTYPE_GETINFO, ACKRESULT_SUCCESS, (HANDLE)1);
}

// nothing to do here because weather proto do not need to retrieve contact info form network
// so just return a 0
INT_PTR WeatherGetInfo(WPARAM, LPARAM lParam)
{
	CCSDATA *ccs = (CCSDATA *)lParam;
	mir_forkthread(AckThreadProc, (void*)ccs->hContact);
	return 0;
}

// avatars
static const wchar_t *statusStr[] = { L"Fog", L"Snow", L"Rain", L"PCloudy", L"Cloudy", L"Sunny", L"NA" };
static const WORD statusValue[] = { FOG, SNOW, RAIN, PCLOUDY, CLOUDY, SUNNY, NA };

INT_PTR WeatherGetAvatarInfo(WPARAM, LPARAM lParam)
{
	wchar_t szSearchPath[MAX_PATH], *chop;
	unsigned  i;
	PROTO_AVATAR_INFORMATION *pai = (PROTO_AVATAR_INFORMATION*)lParam;

	GetModuleFileName(GetModuleHandle(nullptr), szSearchPath, _countof(szSearchPath));
	chop = wcsrchr(szSearchPath, '\\');

	if (chop) *chop = '\0';
	else szSearchPath[0] = 0;

	int iStatus = g_plugin.getWord(pai->hContact, "StatusIcon");
	for (i = 0; i < 10; i++)
		if (statusValue[i] == iStatus)
			break;

	if (i >= 10)
		return GAIR_NOAVATAR;

	pai->format = PA_FORMAT_PNG;
	mir_snwprintf(pai->filename, L"%s\\Plugins\\Weather\\%s.png", szSearchPath, statusStr[i]);
	if (_waccess(pai->filename, 4) == 0)
		return GAIR_SUCCESS;

	pai->format = PA_FORMAT_GIF;
	mir_snwprintf(pai->filename, L"%s\\Plugins\\Weather\\%s.gif", szSearchPath, statusStr[i]);
	if (_waccess(pai->filename, 4) == 0)
		return GAIR_SUCCESS;

	pai->format = PA_FORMAT_UNKNOWN;
	pai->filename[0] = 0;
	return GAIR_NOAVATAR;
}

void AvatarDownloaded(MCONTACT hContact)
{
	PROTO_AVATAR_INFORMATION ai = { 0 };
	ai.hContact = hContact;

	if (WeatherGetAvatarInfo(GAIF_FORCE, (LPARAM)&ai) == GAIR_SUCCESS)
		ProtoBroadcastAck(MODULENAME, hContact, ACKTYPE_AVATAR, ACKRESULT_SUCCESS, &ai);
	else
		ProtoBroadcastAck(MODULENAME, hContact, ACKTYPE_AVATAR, ACKRESULT_STATUS, nullptr);
}

static void __cdecl WeatherGetAwayMsgThread(void *arg)
{
	Sleep(100);

	MCONTACT hContact = (DWORD_PTR)arg;
	DBVARIANT dbv;
	if (!db_get_ws(hContact, "CList", "StatusMsg", &dbv)) {
		ProtoBroadcastAck(MODULENAME, hContact, ACKTYPE_AWAYMSG, ACKRESULT_SUCCESS, (HANDLE)1, (LPARAM)dbv.pwszVal);
		db_free(&dbv);
	}
	else ProtoBroadcastAck(MODULENAME, hContact, ACKTYPE_AWAYMSG, ACKRESULT_SUCCESS, (HANDLE)1);
}

static INT_PTR WeatherGetAwayMsg(WPARAM, LPARAM lParam)
{
	CCSDATA* ccs = (CCSDATA*)lParam;
	if (ccs == nullptr)
		return 0;

	mir_forkthread(WeatherGetAwayMsgThread, (void*)ccs->hContact);
	return 1;
}

//============  PROTOCOL INITIALIZATION  ============
// protocol services
void InitServices(void)
{
	CreateProtoServiceFunction(MODULENAME, PS_GETCAPS, WeatherGetCaps);
	CreateProtoServiceFunction(MODULENAME, PS_GETNAME, WeatherGetName);
	CreateProtoServiceFunction(MODULENAME, PS_LOADICON, WeatherLoadIcon);
	CreateProtoServiceFunction(MODULENAME, PS_SETSTATUS, WeatherSetStatus);
	CreateProtoServiceFunction(MODULENAME, PS_GETSTATUS, WeatherGetStatus);
	CreateProtoServiceFunction(MODULENAME, PS_BASICSEARCH, WeatherBasicSearch);
	CreateProtoServiceFunction(MODULENAME, PS_SEARCHBYEMAIL, WeatherBasicSearch);
	CreateProtoServiceFunction(MODULENAME, PS_ADDTOLIST, WeatherAddToList);
	CreateProtoServiceFunction(MODULENAME, PSS_GETINFO, WeatherGetInfo);
	CreateProtoServiceFunction(MODULENAME, PS_GETAVATARINFO, WeatherGetAvatarInfo);
	CreateProtoServiceFunction(MODULENAME, PSS_GETAWAYMSG, WeatherGetAwayMsg);
	CreateProtoServiceFunction(MODULENAME, PS_CREATEADVSEARCHUI, WeatherCreateAdvancedSearchUI);
	CreateProtoServiceFunction(MODULENAME, PS_SEARCHBYADVANCED, WeatherAdvancedSearch);

	CreateProtoServiceFunction(MODULENAME, MS_WEATHER_GETDISPLAY, GetDisplaySvcFunc);
}

//============  MENU INITIALIZATION  ============

void UpdateMenu(BOOL State)
{
	// update option setting
	opt.CAutoUpdate = State;
	g_plugin.setByte("AutoUpdate", (BYTE)State);

	if (State) { // to enable auto-update
		Menu_ModifyItem(hEnableDisableMenu, LPGENW("Auto Update Enabled"), g_plugin.getIconHandle(IDI_ICON));
		opt.AutoUpdate = 1;
	}
	else { // to disable auto-update
		Menu_ModifyItem(hEnableDisableMenu, LPGENW("Auto Update Disabled"), g_plugin.getIconHandle(IDI_DISABLED));
		opt.AutoUpdate = 0;
	}

	CallService(MS_TTB_SETBUTTONSTATE, (WPARAM)hTBButton, !State ? TTBST_PUSHED : 0);
}

void UpdatePopupMenu(BOOL State)
{
	// update option setting
	opt.UsePopup = State;
	g_plugin.setByte("UsePopup", (BYTE)opt.UsePopup);

	if (State) // to enable popup
		Menu_ModifyItem(hEnableDisablePopupMenu, LPGENW("Disable &weather notification"), g_plugin.getIconHandle(IDI_POPUP));
	else // to disable popup
		Menu_ModifyItem(hEnableDisablePopupMenu, LPGENW("Enable &weather notification"), g_plugin.getIconHandle(IDI_NOPOPUP));
}

// update the weather auto-update menu item when click on it
INT_PTR EnableDisableCmd(WPARAM wParam, LPARAM lParam)
{
	UpdateMenu(wParam == TRUE ? (BOOL)lParam : !opt.CAutoUpdate);
	return 0;
}

// update the weather popup menu item when click on it
INT_PTR MenuitemNotifyCmd(WPARAM, LPARAM)
{
	UpdatePopupMenu(!opt.UsePopup);
	return 0;
}

// adding weather contact menus
// copied and modified form "modified MSN Protocol"
void AddMenuItems(void)
{
	CMenuItem mi(&g_plugin);

	// contact menu
	SET_UID(mi, 0x266ef52b, 0x869a, 0x4cac, 0xa9, 0xf8, 0xea, 0x5b, 0xb8, 0xab, 0xe0, 0x24);
	CreateServiceFunction(MS_WEATHER_UPDATE, UpdateSingleStation);
	mi.position = -0x7FFFFFFA;
	mi.hIcolibItem = g_plugin.getIconHandle(IDI_UPDATE);
	mi.name.a = LPGEN("Update Weather");
	mi.pszService = MS_WEATHER_UPDATE;
	Menu_AddContactMenuItem(&mi, MODULENAME);

	SET_UID(mi, 0x45361b4, 0x8de, 0x44b4, 0x8f, 0x11, 0x9b, 0xe9, 0x6e, 0xa8, 0x83, 0x54);
	CreateServiceFunction(MS_WEATHER_REFRESH, UpdateSingleRemove);
	mi.position = -0x7FFFFFF9;
	mi.hIcolibItem = g_plugin.getIconHandle(IDI_UPDATE2);
	mi.name.a = LPGEN("Remove Old Data then Update");
	mi.pszService = MS_WEATHER_REFRESH;
	Menu_AddContactMenuItem(&mi, MODULENAME);

	SET_UID(mi, 0x4232975e, 0xb181, 0x46a5, 0xb7, 0x6e, 0xd2, 0x5f, 0xef, 0xb8, 0xc4, 0x4d);
	CreateServiceFunction(MS_WEATHER_BRIEF, BriefInfoSvc);
	mi.position = -0x7FFFFFF8;
	mi.hIcolibItem = g_plugin.getIconHandle(IDI_S);
	mi.name.a = LPGEN("Brief Information");
	mi.pszService = MS_WEATHER_BRIEF;
	Menu_AddContactMenuItem(&mi, MODULENAME);

	SET_UID(mi, 0x3d6ed729, 0xd49a, 0x4ae9, 0x8e, 0x2, 0x9f, 0xe0, 0xf0, 0x2c, 0xcc, 0xb1);
	CreateServiceFunction(MS_WEATHER_COMPLETE, LoadForecast);
	mi.position = -0x7FFFFFF7;
	mi.hIcolibItem = g_plugin.getIconHandle(IDI_READ);
	mi.name.a = LPGEN("Read Complete Forecast");
	mi.pszService = MS_WEATHER_COMPLETE;
	Menu_AddContactMenuItem(&mi, MODULENAME);

	SET_UID(mi, 0xc4b6c5e0, 0x13c3, 0x4e02, 0x8a, 0xeb, 0xeb, 0x8a, 0xe2, 0x66, 0x40, 0xd4);
	CreateServiceFunction(MS_WEATHER_MAP, WeatherMap);
	mi.position = -0x7FFFFFF6;
	mi.hIcolibItem = g_plugin.getIconHandle(IDI_MAP);
	mi.name.a = LPGEN("Weather Map");
	mi.pszService = MS_WEATHER_MAP;
	Menu_AddContactMenuItem(&mi, MODULENAME);

	SET_UID(mi, 0xee3ad7f4, 0x3377, 0x4e4c, 0x8f, 0x3c, 0x3b, 0xf5, 0xd4, 0x86, 0x28, 0x25);
	CreateServiceFunction(MS_WEATHER_LOG, ViewLog);
	mi.position = -0x7FFFFFF5;
	mi.hIcolibItem = g_plugin.getIconHandle(IDI_LOG);
	mi.name.a = LPGEN("View Log");
	mi.pszService = MS_WEATHER_LOG;
	Menu_AddContactMenuItem(&mi, MODULENAME);

	SET_UID(mi, 0x1b01cd6a, 0xe5ee, 0x42b4, 0xa1, 0x6d, 0x43, 0xb9, 0x4, 0x58, 0x43, 0x2e);
	CreateServiceFunction(MS_WEATHER_EDIT, EditSettings);
	mi.position = -0x7FFFFFF4;
	mi.hIcolibItem = g_plugin.getIconHandle(IDI_EDIT);
	mi.name.a = LPGEN("Edit Settings");
	mi.pszService = MS_WEATHER_EDIT;
	Menu_AddContactMenuItem(&mi, MODULENAME);

	// adding main menu items
	mi.root = g_plugin.addRootMenu(MO_MAIN, LPGENW("Weather"), 500099000);
	Menu_ConfigureItem(mi.root, MCI_OPT_UID, "82809D2F-2CF0-4E15-9350-D257A7748552");

	SET_UID(mi, 0x5ad16188, 0xe0a0, 0x4c31, 0x85, 0xc3, 0xe4, 0x85, 0x79, 0x7e, 0x4b, 0x9c);
	CreateServiceFunction(MS_WEATHER_ENABLED, EnableDisableCmd);
	mi.name.a = LPGEN("Enable/Disable Weather Update");
	mi.hIcolibItem = g_plugin.getIconHandle(IDI_ICON);
	mi.position = 10100001;
	mi.pszService = MS_WEATHER_ENABLED;
	hEnableDisableMenu = Menu_AddMainMenuItem(&mi);
	UpdateMenu(opt.AutoUpdate);

	SET_UID(mi, 0x2b1c2054, 0x2991, 0x4025, 0x87, 0x73, 0xb6, 0xf7, 0x85, 0xac, 0xc7, 0x37);
	CreateServiceFunction(MS_WEATHER_UPDATEALL, UpdateAllInfo);
	mi.position = 20100001;
	mi.hIcolibItem = g_plugin.getIconHandle(IDI_UPDATE);
	mi.name.a = LPGEN("Update All Weather");
	mi.pszService = MS_WEATHER_UPDATEALL;
	Menu_AddMainMenuItem(&mi);

	SET_UID(mi, 0x8234c00e, 0x788e, 0x424f, 0xbc, 0xc4, 0x2, 0xfd, 0x67, 0x58, 0x2d, 0x19);
	CreateServiceFunction(MS_WEATHER_REFRESHALL, UpdateAllRemove);
	mi.position = 20100002;
	mi.hIcolibItem = g_plugin.getIconHandle(IDI_UPDATE2);
	mi.name.a = LPGEN("Remove Old Data then Update All");
	mi.pszService = MS_WEATHER_REFRESHALL;
	Menu_AddMainMenuItem(&mi);

	SET_UID(mi, 0xdc5411cb, 0xb7c7, 0x443b, 0x88, 0x5a, 0x90, 0x24, 0x43, 0xde, 0x54, 0x3e);
	CreateServiceFunction(MODULENAME "/PopupMenu", MenuitemNotifyCmd);
	mi.name.a = LPGEN("Weather Notification");
	mi.hIcolibItem = g_plugin.getIconHandle(IDI_POPUP);
	mi.position = 0;
	mi.root = g_plugin.addRootMenu(MO_MAIN, LPGENW("Popups"), 0);
	mi.pszService = MODULENAME "/PopupMenu";
	hEnableDisablePopupMenu = Menu_AddMainMenuItem(&mi);
	UpdatePopupMenu(opt.UsePopup);

	if (ServiceExists(MS_CLIST_FRAMES_ADDFRAME)) {
		SET_UID(mi, 0xe193fe9b, 0xf6ad, 0x41ac, 0x95, 0x29, 0x45, 0x4, 0x44, 0xb1, 0xeb, 0x5d);
		mi.pszService = "Weather/mwin_menu";
		CreateServiceFunction(mi.pszService, Mwin_MenuClicked);
		mi.position = -0x7FFFFFF0;
		mi.hIcolibItem = nullptr;
		mi.root = nullptr;
		mi.name.a = LPGEN("Display in a frame");
		hMwinMenu = Menu_AddContactMenuItem(&mi, MODULENAME);
	}
}
