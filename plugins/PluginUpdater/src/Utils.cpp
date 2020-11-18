/*
Copyright (C) 2010 Mataes

This is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with this file; see the file license.txt.  If
not, write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.
*/

#include "stdafx.h"

HNETLIBUSER hNetlibUser = nullptr;

/////////////////////////////////////////////////////////////////////////////////////

IconItem iconList[] =
{
	{ LPGEN("Check for updates"),"check_update", IDI_MENU },
	{ LPGEN("Plugin info"), "info", IDI_INFO },
	{ LPGEN("Component list"),"plg_list", IDI_PLGLIST }
};

void InitIcoLib()
{
	g_plugin.registerIcon(MODULEA, iconList);
}

void InitNetlib()
{
	NETLIBUSER nlu = {};
	nlu.flags = NUF_OUTGOING | NUF_INCOMING | NUF_HTTPCONNS | NUF_UNICODE;
	nlu.szDescriptiveName.w = TranslateT("Plugin Updater HTTP connections");
	nlu.szSettingsModule = MODULENAME;
	hNetlibUser = Netlib_RegisterUser(&nlu);
}

void UnloadNetlib()
{
	Netlib_CloseHandle(hNetlibUser);
	hNetlibUser = nullptr;
}

int CompareHashes(const ServListEntry *p1, const ServListEntry *p2)
{
	return _wcsicmp(p1->m_name, p2->m_name);
}

bool ParseHashes(const wchar_t *pwszUrl, ptrW &baseUrl, SERVLIST &arHashes)
{
	REPLACEVARSARRAY vars[2];
	vars[0].key.w = L"platform";
	#ifdef _WIN64
		vars[0].value.w = L"64";
	#else
		vars[0].value.w = L"32";
	#endif
	vars[1].key.w = vars[1].value.w = nullptr;

	baseUrl = Utils_ReplaceVarsW(pwszUrl, 0, vars);

	// Download version info
	FILEURL pFileUrl;
	mir_snwprintf(pFileUrl.wszDownloadURL, L"%s/hashes.zip", baseUrl.get());
	mir_snwprintf(pFileUrl.wszDiskPath, L"%s\\hashes.zip", g_wszTempPath);
	pFileUrl.CRCsum = 0;

	HNETLIBCONN nlc = nullptr;
	bool ret = DownloadFile(&pFileUrl, nlc);
	Netlib_CloseHandle(nlc);

	if (!ret) {
		Netlib_LogfW(hNetlibUser, L"Downloading list of available updates from %s failed", baseUrl.get());
		ShowPopup(TranslateT("Plugin Updater"), TranslateT("An error occurred while checking for new updates."), POPUP_TYPE_ERROR);
		Skin_PlaySound("updatefailed");
		return false;
	}

	if (unzip(pFileUrl.wszDiskPath, g_wszTempPath, nullptr, true)) {
		Netlib_LogfW(hNetlibUser, L"Unzipping list of available updates from %s failed", baseUrl.get());
		ShowPopup(TranslateT("Plugin Updater"), TranslateT("An error occurred while checking for new updates."), POPUP_TYPE_ERROR);
		Skin_PlaySound("updatefailed");
		return false;
	}

	DeleteFile(pFileUrl.wszDiskPath);

	TFileName wszTmpIni;
	mir_snwprintf(wszTmpIni, L"%s\\hashes.txt", g_wszTempPath);
	FILE *fp = _wfopen(wszTmpIni, L"r");
	if (!fp) {
		Netlib_LogfW(hNetlibUser, L"Opening %s failed", g_wszTempPath);
		ShowPopup(TranslateT("Plugin Updater"), TranslateT("An error occurred while checking for new updates."), POPUP_TYPE_ERROR);
		return false;
	}

	bool bDoNotSwitchToStable = false;
	char str[200];
	while (fgets(str, _countof(str), fp) != nullptr) {
		rtrim(str);
		// Do not allow the user to switch back to stable
		if (!strcmp(str, "DoNotSwitchToStable")) {
			bDoNotSwitchToStable = true;
		}
		else if (str[0] != ';') { // ';' marks a comment
			Netlib_Logf(hNetlibUser, "Update: %s", str);
			char *p = strchr(str, ' ');
			if (p != nullptr) {
				*p++ = 0;
				_strlwr(p);

				int dwCrc32;
				char *p1 = strchr(p, ' ');
				if (p1 == nullptr)
					dwCrc32 = 0;
				else {
					*p1++ = 0;
					sscanf(p1, "%08x", &dwCrc32);
				}
				arHashes.insert(new ServListEntry(str, p, dwCrc32));
			}
		}
	}
	fclose(fp);
	DeleteFileW(wszTmpIni);

	if (bDoNotSwitchToStable) {
		g_plugin.setByte(DB_SETTING_DONT_SWITCH_TO_STABLE, 1);
		// Reset setting if needed
		int UpdateMode = g_plugin.getByte(DB_SETTING_UPDATE_MODE, UPDATE_MODE_STABLE);
		if (UpdateMode == UPDATE_MODE_STABLE)
			g_plugin.setByte(DB_SETTING_UPDATE_MODE, UPDATE_MODE_TRUNK);
	}
	else g_plugin.setByte(DB_SETTING_DONT_SWITCH_TO_STABLE, 0);

	return true;
}

/////////////////////////////////////////////////////////////////////////////////////////
// Single file HTTP transaction

bool DownloadFile(FILEURL *pFileURL, HNETLIBCONN &nlc)
{
	char szMirVer[100];
	Miranda_GetVersionText(szMirVer, _countof(szMirVer));
	if (auto *p = strchr(szMirVer, '('))
		*p = 0;
	rtrim(szMirVer);

	char szOsVer[100];
	OS_GetShortString(szOsVer, _countof(szOsVer));

	CMStringA szUserAgent("Miranda NG/");
	szUserAgent.Append(szMirVer);
	szUserAgent.AppendFormat(" (%s", szOsVer);
	#ifdef _WIN64
		szUserAgent.Append("; Win64; x64");
	#endif
	szUserAgent.Append(")");

	NETLIBHTTPHEADER headers[4] = {
		{ "User-Agent", szUserAgent.GetBuffer() },
		{ "Connection", "close" },
		{ "Cache-Control", "no-cache" },
		{ "Pragma", "no-cache" }
	};

	ptrA szUrl(mir_u2a(pFileURL->wszDownloadURL));

	NETLIBHTTPREQUEST nlhr = {};
	nlhr.cbSize = sizeof(nlhr);
	nlhr.flags = NLHRF_DUMPASTEXT | NLHRF_HTTP11 | NLHRF_PERSISTENT;
	nlhr.requestType = REQUEST_GET;
	nlhr.nlc = nlc;
	nlhr.szUrl = szUrl;
	nlhr.headersCount = _countof(headers);
	nlhr.headers = headers;

	for (int i = 0; i < MAX_RETRIES; i++) {
		Netlib_LogfW(hNetlibUser, L"Downloading file %s to %s (attempt %d)", pFileURL->wszDownloadURL, pFileURL->wszDiskPath, i + 1);
		NLHR_PTR pReply(Netlib_HttpTransaction(hNetlibUser, &nlhr));
		if (pReply) {
			nlc = pReply->nlc;
			if ((200 == pReply->resultCode) && (pReply->dataLength > 0)) {
				// Check CRC sum
				if (pFileURL->CRCsum) {
					int crc = crc32(0, (unsigned char *)pReply->pData, pReply->dataLength);
					if (crc != pFileURL->CRCsum) {
						// crc check failed, try again
						Netlib_LogfW(hNetlibUser, L"crc check failed for file %s", pFileURL->wszDiskPath);
						continue;
					}
				}

				DWORD dwBytes;
				HANDLE hFile = CreateFile(pFileURL->wszDiskPath, GENERIC_READ | GENERIC_WRITE, NULL, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (hFile != INVALID_HANDLE_VALUE) {
					// write the downloaded file directly
					WriteFile(hFile, pReply->pData, (DWORD)pReply->dataLength, &dwBytes, nullptr);
					CloseHandle(hFile);
				}
				else {
					// try to write it via PU stub
					TFileName wszTempFile;
					mir_snwprintf(wszTempFile, L"%s\\pulocal.tmp", g_wszTempPath);
					hFile = CreateFile(wszTempFile, GENERIC_READ | GENERIC_WRITE, NULL, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
					if (hFile != INVALID_HANDLE_VALUE) {
						WriteFile(hFile, pReply->pData, (DWORD)pReply->dataLength, &dwBytes, nullptr);
						CloseHandle(hFile);
						PU::SafeMoveFile(wszTempFile, pFileURL->wszDiskPath);
					}
				}
				return true;
			}
			Netlib_LogfW(hNetlibUser, L"Downloading file %s failed with error %d", pFileURL->wszDownloadURL, pReply->resultCode);
		}
		else {
			Netlib_LogfW(hNetlibUser, L"Downloading file %s failed, host is propably temporary down.", pFileURL->wszDownloadURL);
			nlc = nullptr;
		}
	}

	Netlib_LogfW(hNetlibUser, L"Downloading file %s failed, giving up", pFileURL->wszDownloadURL);
	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////
// Folder creation

static int __cdecl CompareDirs(const CMStringW *s1, const CMStringW *s2)
{
	return mir_wstrcmp(s1->c_str(), s2->c_str());
}

void CreateWorkFolders(TFileName &wszTempFolder, TFileName &wszBackupFolder)
{
	SYSTEMTIME st;
	GetLocalTime(&st);
	mir_snwprintf(wszBackupFolder, L"%s\\Backups\\BKP%04d-%02d-%02d %02d-%02d-%02d-%03d", g_wszRoot, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	PU::SafeCreateDirectory(wszBackupFolder);

	mir_snwprintf(wszTempFolder, L"%s\\Temp", g_wszRoot);
	PU::SafeCreateDirectory(wszTempFolder);
}

/////////////////////////////////////////////////////////////////////////////////////////
// Folder removal

void RemoveBackupFolders()
{
	TFileName wszMask;
	mir_snwprintf(wszMask, L"%s\\Backups\\BKP*", g_wszRoot);

	WIN32_FIND_DATAW fdata;
	HANDLE hFind = FindFirstFileW(wszMask, &fdata);
	if (!hFind)
		return;

	// sort folder names alphabetically
	OBJLIST<CMStringW> arNames(1, CompareDirs);
	do {
		if (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			arNames.insert(new CMStringW(fdata.cFileName));
	} while (FindNextFileW(hFind, &fdata));

	FindClose(hFind);

	// remove all folders with lesser dates if there're more than 10 folders
	if (PU::PrepareEscalation()) {
		while (arNames.getCount() > g_plugin.iNumberBackups) {
			mir_snwprintf(wszMask, L"%s\\Backups\\%s", g_wszRoot, arNames[0].c_str());
			PU::SafeDeleteDirectory(wszMask);
			arNames.remove(00);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////
// moves updated files back to Miranda's folder

static void SafeMoveFolder(const wchar_t *wszSrc, const wchar_t *wszDest)
{
	TFileName wszNewSrc, wszNewDest;
	mir_snwprintf(wszNewSrc, L"%s\\*", wszSrc);

	WIN32_FIND_DATAW fdata;
	HANDLE hFind = FindFirstFileW(wszNewSrc, &fdata);
	if (!hFind)
		return;

	do {
		if (!mir_wstrcmp(fdata.cFileName, L".") || !mir_wstrcmp(fdata.cFileName, L".."))
			continue;

		mir_snwprintf(wszNewSrc, L"%s\\%s", wszSrc, fdata.cFileName);
		mir_snwprintf(wszNewDest, L"%s\\%s", wszDest, fdata.cFileName);

		if (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			SafeMoveFolder(wszNewSrc, wszNewDest);
		else
			PU::SafeMoveFile(wszNewSrc, wszNewDest);
	}
		while (FindNextFileW(hFind, &fdata));

	FindClose(hFind);
}

void RollbackChanges(TFileName &pwszBackupFolder)
{
	VARSW dirname(L"%miranda_path%");
	SafeMoveFolder(pwszBackupFolder, dirname);

	PU::SafeDeleteDirectory(pwszBackupFolder);
}

int BackupFile(wchar_t *pwszSrcFileName, wchar_t *pwszBackFileName)
{
	if (_waccess(pwszSrcFileName, 0))
		return 0;

	PU::SafeCreateFilePath(pwszBackFileName);

	return PU::SafeMoveFile(pwszSrcFileName, pwszBackFileName);
}

/////////////////////////////////////////////////////////////////////////////////////////

char* StrToLower(char *str)
{
	for (int i = 0; str[i]; i++)
		str[i] = tolower(str[i]);

	return str;
}
