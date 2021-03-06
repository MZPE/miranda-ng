/*
Copyright (C) 2006-2007 Scott Ellis
Copyright (C) 2007-2011 Jan Holub

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

int iCodePage = CP_ACP;

bool a2w(const char *as, wchar_t *buff, int bufflen)
{
	if (as) MultiByteToWideChar(iCodePage, 0, as, -1, buff, bufflen);
	return true;
}

bool w2a(const wchar_t *ws, char *buff, int bufflen)
{
	if (ws) WideCharToMultiByte(iCodePage, 0, ws, -1, buff, bufflen, nullptr, nullptr);
	return true;
}

bool utf2w(const char *us, wchar_t *buff, int bufflen)
{
	if (us) MultiByteToWideChar(CP_UTF8, 0, us, -1, buff, bufflen);
	return true;
}

bool w2utf(const wchar_t *ws, char *buff, int bufflen)
{
	if (ws) WideCharToMultiByte(CP_UTF8, 0, ws, -1, buff, bufflen, nullptr, nullptr);
	return true;
}

bool a2utf(const char *as, char *buff, int bufflen)
{
	if (!as) return false;

	wchar_t *ws = a2w(as);
	if (ws) WideCharToMultiByte(CP_UTF8, 0, ws, -1, buff, bufflen, nullptr, nullptr);
	mir_free(ws);
	return true;
}

bool utf2a(const char *us, char *buff, int bufflen)
{
	if (!us) return false;

	wchar_t *ws = utf2w(us);
	if (ws) WideCharToMultiByte(iCodePage, 0, ws, -1, buff, bufflen, nullptr, nullptr);
	mir_free(ws);
	return true;
}


bool t2w(const wchar_t *ts, wchar_t *buff, int bufflen)
{
	wcsncpy(buff, ts, bufflen);
	return true;
}

bool w2t(const wchar_t *ws, wchar_t *buff, int bufflen)
{
	wcsncpy(buff, ws, bufflen);
	return true;
}

bool t2a(const wchar_t *ts, char *buff, int bufflen)
{
	return w2a(ts, buff, bufflen);
}

bool a2t(const char *as, wchar_t *buff, int bufflen)
{
	return a2w(as, buff, bufflen);
}

bool t2utf(const wchar_t *ts, char *buff, int bufflen)
{
	return w2utf(ts, buff, bufflen);
}

bool utf2t(const char *us, wchar_t *buff, int bufflen)
{
	return utf2w(us, buff, bufflen);
}

wchar_t *utf2w(const char *us)
{
	if (us) {
		int size = MultiByteToWideChar(CP_UTF8, 0, us, -1, nullptr, 0);
		wchar_t *buff = (wchar_t *)mir_alloc(size * sizeof(wchar_t));
		MultiByteToWideChar(CP_UTF8, 0, us, -1, buff, size);
		return buff;
	}
	else {
		return nullptr;
	}
}

char *w2utf(const wchar_t *ws)
{
	if (ws) {
		int size = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
		char *buff = (char *)mir_alloc(size);
		WideCharToMultiByte(CP_UTF8, 0, ws, -1, buff, size, nullptr, nullptr);
		return buff;
	}
	else {
		return nullptr;
	}
}

wchar_t *a2w(const char *as)
{
	int size = MultiByteToWideChar(iCodePage, 0, as, -1, nullptr, 0);
	wchar_t *buff = (wchar_t *)mir_alloc(size * sizeof(wchar_t));
	MultiByteToWideChar(iCodePage, 0, as, -1, buff, size);
	return buff;
}

char *w2a(const wchar_t *ws)
{
	int size = WideCharToMultiByte(iCodePage, 0, ws, -1, nullptr, 0, nullptr, nullptr);
	char *buff = (char *)mir_alloc(size);
	WideCharToMultiByte(iCodePage, 0, ws, -1, buff, size, nullptr, nullptr);
	return buff;
}

char *utf2a(const char *utfs)
{
	wchar_t *ws = utf2w(utfs);
	char *ret = w2a(ws);
	mir_free(ws);
	return ret;
}

char *a2utf(const char *as)
{
	wchar_t *ws = a2w(as);
	char *ret = w2utf(ws);
	mir_free(ws);
	return ret;
}

wchar_t *w2t(const wchar_t *ws)
{
	return mir_wstrdup(ws);
}

wchar_t *t2w(const wchar_t *ts)
{
	return mir_wstrdup(ts);
}


char *t2a(const wchar_t *ts)
{
	return w2a(ts);
}

wchar_t *a2t(const char *as)
{
	return a2w(as);
}

wchar_t *utf2t(const char *utfs)
{
	return utf2w(utfs);
}

char *t2utf(const wchar_t *ts)
{
	return w2utf(ts);
}

wchar_t *myfgets(wchar_t *Buf, int MaxCount, FILE *File)
{
	fgetws(Buf, MaxCount, File);
	for (size_t i = mir_wstrlen(Buf) - 1; ; i--) {
		if (Buf[i] == '\n' || Buf[i] == ' ')
			Buf[i] = 0;
		else
			break;
	}

	CharLower(Buf);
	return Buf;
}
