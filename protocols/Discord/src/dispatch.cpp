/*
Copyright © 2016-18 Miranda NG team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "stdafx.h"

#pragma pack(4)

/////////////////////////////////////////////////////////////////////////////////////////

struct CDiscordCommand
{
	const wchar_t *szCommandId;
	GatewayHandlerFunc pFunc;
}
static handlers[] = // these structures must me sorted alphabetically
{
	{ L"CHANNEL_CREATE", &CDiscordProto::OnCommandChannelCreated },
	{ L"CHANNEL_DELETE", &CDiscordProto::OnCommandChannelDeleted },
	{ L"CHANNEL_UPDATE", &CDiscordProto::OnCommandChannelUpdated },

	{ L"GUILD_CREATE", &CDiscordProto::OnCommandGuildCreated },
	{ L"GUILD_DELETE", &CDiscordProto::OnCommandGuildDeleted },
	{ L"GUILD_MEMBER_ADD", &CDiscordProto::OnCommandGuildMemberAdded },
	{ L"GUILD_MEMBER_REMOVE", &CDiscordProto::OnCommandGuildMemberRemoved },
	{ L"GUILD_MEMBER_UPDATE", &CDiscordProto::OnCommandGuildMemberUpdated },
	{ L"GUILD_ROLE_CREATE", &CDiscordProto::OnCommandRoleCreated },
	{ L"GUILD_ROLE_DELETE", &CDiscordProto::OnCommandRoleDeleted },
	{ L"GUILD_ROLE_UPDATE", &CDiscordProto::OnCommandRoleCreated },
	{ L"GUILD_SYNC", &CDiscordProto::OnCommandGuildSync },

	{ L"MESSAGE_ACK", &CDiscordProto::OnCommandMessageAck },
	{ L"MESSAGE_CREATE", &CDiscordProto::OnCommandMessageCreate },
	{ L"MESSAGE_UPDATE", &CDiscordProto::OnCommandMessageUpdate },

	{ L"PRESENCE_UPDATE", &CDiscordProto::OnCommandPresence },

	{ L"READY", &CDiscordProto::OnCommandReady },

	{ L"RELATIONSHIP_ADD", &CDiscordProto::OnCommandFriendAdded },
	{ L"RELATIONSHIP_REMOVE", &CDiscordProto::OnCommandFriendRemoved },

	{ L"TYPING_START", &CDiscordProto::OnCommandTyping },

	{ L"USER_SETTINGS_UPDATE", &CDiscordProto::OnCommandUserSettingsUpdate },
	{ L"USER_UPDATE", &CDiscordProto::OnCommandUserUpdate },
};

static int __cdecl pSearchFunc(const void *p1, const void *p2)
{
	return wcscmp(((CDiscordCommand*)p1)->szCommandId, ((CDiscordCommand*)p2)->szCommandId);
}

GatewayHandlerFunc CDiscordProto::GetHandler(const wchar_t *pwszCommand)
{
	CDiscordCommand tmp = { pwszCommand, nullptr };
	CDiscordCommand *p = (CDiscordCommand*)bsearch(&tmp, handlers, _countof(handlers), sizeof(handlers[0]), pSearchFunc);
	return (p != nullptr) ? p->pFunc : nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////
// channel operations

void CDiscordProto::OnCommandChannelCreated(const JSONNode &pRoot)
{
	SnowFlake guildId = ::getId(pRoot["guild_id"]);
	if (guildId == 0) {
		// private channel, created for a contact
		const JSONNode &members = pRoot["recipients"];
		for (auto it = members.begin(); it != members.end(); ++it) {
			CDiscordUser *pUser = PrepareUser(*it);
			pUser->lastMsgId = ::getId(pRoot["last_message_id"]);
			pUser->channelId = ::getId(pRoot["id"]);
			setId(pUser->hContact, DB_KEY_CHANNELID, pUser->channelId);
		}
	}
	else {
		// group channel for a guild
		CDiscordGuild *pGuild = FindGuild(guildId);
		if (pGuild) {
			CDiscordUser *pUser = ProcessGuildChannel(pGuild, pRoot);
			if (pUser)
				CreateChat(pGuild, pUser);
		}
	}
}

void CDiscordProto::OnCommandChannelDeleted(const JSONNode &pRoot)
{
	CDiscordUser *pUser = FindUserByChannel(::getId(pRoot["id"]));
	if (pUser == nullptr)
		return;

	SnowFlake guildId = ::getId(pRoot["guild_id"]);
	if (guildId == 0) {
		pUser->channelId = pUser->lastMsgId = 0;
		delSetting(pUser->hContact, DB_KEY_CHANNELID);
	}
	else {
		CDiscordGuild *pGuild = FindGuild(guildId);
		if (pGuild != nullptr)
			Chat_Terminate(m_szModuleName, pUser->wszUsername, true);
	}
}

void CDiscordProto::OnCommandChannelUpdated(const JSONNode &pRoot)
{
	CDiscordUser *pUser = FindUserByChannel(::getId(pRoot["id"]));
	if (pUser == nullptr)
		return;

	pUser->lastMsgId = ::getId(pRoot["last_message_id"]);

	SnowFlake guildId = ::getId(pRoot["guild_id"]);
	if (guildId != 0) {
		CDiscordGuild *pGuild = FindGuild(guildId);
		if (pGuild == nullptr)
			return;

		CMStringW wszName = pRoot["name"].as_mstring();
		if (!wszName.IsEmpty()) {
			CMStringW wszNewName = pGuild->wszName + L"#" + wszName;
			Chat_ChangeSessionName(m_szModuleName, pUser->wszUsername, wszNewName);
		}

		CMStringW wszTopic = pRoot["topic"].as_mstring();
		Chat_SetStatusbarText(m_szModuleName, pUser->wszUsername, wszTopic);

		GCEVENT gce = { m_szModuleName, pUser->wszUsername, GC_EVENT_TOPIC };
		gce.ptszText = wszTopic;
		gce.time = time(0);
		Chat_Event(&gce);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////
// reading a new message

void CDiscordProto::OnCommandFriendAdded(const JSONNode &pRoot)
{
	CDiscordUser *pUser = PrepareUser(pRoot["user"]);
	pUser->bIsPrivate = true;
	ProcessType(pUser, pRoot);
}

void CDiscordProto::OnCommandFriendRemoved(const JSONNode &pRoot)
{
	SnowFlake id = ::getId(pRoot["id"]);
	CDiscordUser *pUser = FindUser(id);
	if (pUser != nullptr) {
		if (pUser->hContact)
			if (pUser->bIsPrivate)
				db_delete_contact(pUser->hContact);

		arUsers.remove(pUser);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////
// guild synchronization

void CDiscordProto::OnCommandGuildCreated(const JSONNode &pRoot)
{
	ProcessGuild(pRoot);
	OnCommandGuildSync(pRoot);
}

void CDiscordProto::OnCommandGuildSync(const JSONNode &pRoot)
{
	CDiscordGuild *pGuild = FindGuild(::getId(pRoot["id"]));
	if (pGuild != nullptr)
		ParseGuildContents(pGuild, pRoot);
}

void CDiscordProto::OnCommandGuildDeleted(const JSONNode &pRoot)
{
	CDiscordGuild *pGuild = FindGuild(::getId(pRoot["id"]));
	if (pGuild == nullptr)
		return;

	auto T = arUsers.rev_iter();
	for (auto &it : T)
		if (it->pGuild == pGuild) {
			Chat_Terminate(m_szModuleName, it->wszUsername, true);
			arUsers.remove(T.indexOf(&it));
		}

	Chat_Terminate(m_szModuleName, pRoot["name"].as_mstring(), true);

	arGuilds.remove(pGuild);
}

/////////////////////////////////////////////////////////////////////////////////////////
// guild members

void CDiscordProto::OnCommandGuildMemberAdded(const JSONNode&)
{
}

void CDiscordProto::OnCommandGuildMemberRemoved(const JSONNode &pRoot)
{
	CDiscordGuild *pGuild = FindGuild(::getId(pRoot["guild_id"]));
	if (pGuild == nullptr)
		return;
	
	CMStringW wszUserId = pRoot["user"]["id"].as_mstring();

	for (auto &pUser : arUsers) {
		if (pUser->pGuild != pGuild)
			continue;

		GCEVENT gce = { m_szModuleName, pUser->wszUsername, GC_EVENT_PART };
		gce.time = time(0);
		gce.ptszUID = wszUserId;
		Chat_Event(&gce);
	}
}

void CDiscordProto::OnCommandGuildMemberUpdated(const JSONNode &pRoot)
{
	CDiscordGuild *pGuild = FindGuild(::getId(pRoot["guild_id"]));
	if (pGuild == nullptr)
		return;

	CMStringW wszUserId = pRoot["user"]["id"].as_mstring();
	CDiscordGuildMember *gm = pGuild->FindUser(_wtoi64(wszUserId));
	if (gm == nullptr)
		return;

	gm->wszNick = pRoot["nick"].as_mstring();
	if (gm->wszNick.IsEmpty())
		gm->wszNick = pRoot["user"]["username"].as_mstring() + L"#" + pRoot["user"]["discriminator"].as_mstring();

	for (auto &it : arUsers) {
		if (it->pGuild != pGuild)
			continue;

		CMStringW wszOldNick;
		SESSION_INFO *si = g_chatApi.SM_FindSession(it->wszUsername, m_szModuleName);
		if (si != nullptr) {
			USERINFO *ui = g_chatApi.UM_FindUser(si, wszUserId);
			if (ui != nullptr)
				wszOldNick = ui->pszNick;
		}

		GCEVENT gce = { m_szModuleName, it->wszUsername, GC_EVENT_NICK };
		gce.time = time(0);
		gce.ptszUID = wszUserId;
		gce.ptszNick = wszOldNick;
		gce.ptszText = gm->wszNick;
		Chat_Event(&gce);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////
// roles

void CDiscordProto::OnCommandRoleCreated(const JSONNode &pRoot)
{
	CDiscordGuild *pGuild = FindGuild(::getId(pRoot["guild_id"]));
	if (pGuild != nullptr)
		ProcessRole(pGuild, pRoot["role"]);
}

void CDiscordProto::OnCommandRoleDeleted(const JSONNode &pRoot)
{
	CDiscordGuild *pGuild = FindGuild(::getId(pRoot["guild_id"]));
	if (pGuild == nullptr)
		return;

	SnowFlake id = ::getId(pRoot["role_id"]);
	CDiscordRole *pRole = pGuild->arRoles.find((CDiscordRole*)&id);
	if (pRole == nullptr)
		return;

	int iOldPosition = pRole->position;
	pGuild->arRoles.remove(pRole);

	for (auto &it : pGuild->arRoles)
		if (it->position > iOldPosition)
			it->position--;

	for (auto &it : arUsers) {
		if (it->pGuild != pGuild)
			continue;

		SESSION_INFO *si = g_chatApi.SM_FindSession(it->wszUsername, m_szModuleName);
		if (si != nullptr) {
			g_chatApi.TM_RemoveAll(&si->pStatuses);
			BuildStatusList(pGuild, si);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////
// reading a new message

void CDiscordProto::OnCommandMessageCreate(const JSONNode &pRoot)
{
	OnCommandMessage(pRoot, true);
}

void CDiscordProto::OnCommandMessageUpdate(const JSONNode &pRoot)
{
	OnCommandMessage(pRoot, false);
}

void CDiscordProto::OnCommandMessage(const JSONNode &pRoot, bool bIsNew)
{
	CMStringW wszMessageId = pRoot["id"].as_mstring();
	CMStringW wszUserId = pRoot["author"]["id"].as_mstring();
	SnowFlake userId = _wtoi64(wszUserId);
	SnowFlake msgId = _wtoi64(wszMessageId);

	// try to find a sender by his channel
	SnowFlake channelId = ::getId(pRoot["channel_id"]);
	CDiscordUser *pUser = FindUserByChannel(channelId);
	if (pUser == nullptr) {
		debugLogA("skipping message with unknown channel id=%lld", channelId);
		return;
	}

	char szMsgId[100];
	_i64toa_s(msgId, szMsgId, _countof(szMsgId), 10);

	COwnMessage ownMsg(::getId(pRoot["nonce"]), 0);
	COwnMessage *p = arOwnMessages.find(&ownMsg);
	if (p != nullptr) { // own message? skip it
		ProtoBroadcastAck(pUser->hContact, ACKTYPE_MESSAGE, ACKRESULT_SUCCESS, (HANDLE)p->reqId, (LPARAM)szMsgId);
		debugLogA("skipping own message with nonce=%lld, id=%lld", ownMsg.nonce, msgId);
	}
	else {
		CMStringW wszText = PrepareMessageText(pRoot);
		bool bOurMessage = userId == m_ownId;

		// old message? try to restore it from database
		if (!bIsNew) {
			MEVENT hOldEvent = db_event_getById(m_szModuleName, szMsgId);
			if (hOldEvent) {
				DBEVENTINFO dbei = {};
				dbei.cbBlob = db_event_getBlobSize(hOldEvent);
				dbei.pBlob = (BYTE*)mir_alloc(dbei.cbBlob);
				if (!db_event_get(hOldEvent, &dbei)) {
					ptrW wszOldText(DbEvent_GetTextW(&dbei, CP_UTF8));
					if (wszOldText)
						wszText.Insert(0, wszOldText);
					if (dbei.flags & DBEF_SENT)
						bOurMessage = true;
				}
				mir_free(dbei.pBlob);
			}
		}

		const JSONNode &edited = pRoot["edited_timestamp"];
		if (!edited.isnull())
			wszText.AppendFormat(L" (%s %s)", TranslateT("edited at"), edited.as_mstring().c_str());

		if (pUser->bIsPrivate) {
			// if a message has myself as an author, add some flags
			PROTORECVEVENT recv = {};
			if (bOurMessage)
				recv.flags = PREF_CREATEREAD | PREF_SENT;

			debugLogA("store a message from private user %lld, channel id %lld", pUser->id, pUser->channelId);
			ptrA buf(mir_utf8encodeW(wszText));

			recv.timestamp = (DWORD)StringToDate(pRoot["timestamp"].as_mstring());
			recv.szMessage = buf;
			recv.szMsgId = szMsgId;
			ProtoChainRecvMsg(pUser->hContact, &recv);
		}
		else {
			debugLogA("store a message into the group channel id %lld", channelId);

			SESSION_INFO *si = g_chatApi.SM_FindSession(pUser->wszUsername, m_szModuleName);
			if (si == nullptr) {
				debugLogA("message to unknown channel %lld ignored", channelId);
				return;
			}

			CDiscordGuild *pGuild = pUser->pGuild;
			if (pGuild == nullptr) {
				debugLogA("message to unknown guild ignored");
				return;
			}

			if (userId != 0) {
				CDiscordGuildMember *pm = pGuild->FindUser(userId);
				if (pm == nullptr) {
					pm = new CDiscordGuildMember(userId);
					pm->wszNick = pRoot["nick"].as_mstring();
					if (pm->wszNick.IsEmpty())
						pm->wszNick = pRoot["user"]["username"].as_mstring() + L"#" + pRoot["user"]["discriminator"].as_mstring();
					pGuild->arChatUsers.insert(pm);

					AddGuildUser(pGuild, *pm);
				}
			}

			ParseSpecialChars(si, wszText);

			GCEVENT gce = { m_szModuleName, pUser->wszUsername, GC_EVENT_MESSAGE };
			gce.dwFlags = GCEF_ADDTOLOG;
			gce.ptszUID = wszUserId;
			gce.ptszText = wszText;
			gce.time = (DWORD)StringToDate(pRoot["timestamp"].as_mstring());
			gce.bIsMe = bOurMessage;
			Chat_Event(&gce);

			debugLogW(L"New channel %s message from %s: %s", si->ptszID, gce.ptszUID, gce.ptszText);
		}
	}

	pUser->lastMsgId = msgId;

	SnowFlake lastId = getId(pUser->hContact, DB_KEY_LASTMSGID); // as stored in a database
	if (lastId < msgId)
		setId(pUser->hContact, DB_KEY_LASTMSGID, msgId);
}

/////////////////////////////////////////////////////////////////////////////////////////
// someone changed its status

void CDiscordProto::OnCommandMessageAck(const JSONNode &pRoot)
{
	CDiscordUser *pUser = FindUserByChannel(pRoot["channel_id"]);
	if (pUser != nullptr)
		pUser->lastMsgId = ::getId(pRoot["message_id"]);
}

/////////////////////////////////////////////////////////////////////////////////////////
// someone changed its status

void CDiscordProto::OnCommandPresence(const JSONNode &pRoot)
{
	CDiscordUser *pUser = FindUser(::getId(pRoot["user"]["id"]));
	if (pUser == nullptr)
		return;

	int iStatus = StrToStatus(pRoot["status"].as_mstring());
	if (iStatus != 0)
		setWord(pUser->hContact, "Status", iStatus);

	CMStringW wszGame = pRoot["game"]["name"].as_mstring();
	if (!wszGame.IsEmpty())
		setWString(pUser->hContact, "XStatusMsg", wszGame);
	else
		delSetting(pUser->hContact, "XStatusMsg");

	// check avatar
	CheckAvatarChange(pUser->hContact, pRoot["user"]["avatar"].as_mstring());
}

/////////////////////////////////////////////////////////////////////////////////////////
// gateway session start

void CALLBACK CDiscordProto::HeartbeatTimerProc(HWND, UINT, UINT_PTR id, DWORD)
{
	((CDiscordProto*)id)->GatewaySendHeartbeat();
}

static void __stdcall sttStartTimer(void *param)
{
	CDiscordProto *ppro = (CDiscordProto*)param;
	SetTimer(g_hwndHeartbeat, (UINT_PTR)param, ppro->getHeartbeatInterval(), &CDiscordProto::HeartbeatTimerProc);
}

void CDiscordProto::OnCommandReady(const JSONNode &pRoot)
{
	GatewaySendHeartbeat();
	CallFunctionAsync(sttStartTimer, this);

	m_szGatewaySessionId = pRoot["session_id"].as_mstring();

	const JSONNode &guilds = pRoot["guilds"];
	for (auto it = guilds.begin(); it != guilds.end(); ++it)
		ProcessGuild(*it);

	const JSONNode &relations = pRoot["relationships"];
	for (auto it = relations.begin(); it != relations.end(); ++it) {
		const JSONNode &p = *it;

		CDiscordUser *pUser = PrepareUser(p["user"]);
		ProcessType(pUser, p);
	}

	const JSONNode &pStatuses = pRoot["presences"];
	for (auto it = pStatuses.begin(); it != pStatuses.end(); ++it) {
		const JSONNode &p = *it;

		CDiscordUser *pUser = FindUser(::getId(p["user"]["id"]));
		if (pUser != nullptr)
			setWord(pUser->hContact, "Status", StrToStatus(p["status"].as_mstring()));
	}

	const JSONNode &channels = pRoot["private_channels"];
	for (auto it = channels.begin(); it != channels.end(); ++it) {
		const JSONNode &p = *it;

		CDiscordUser *pUser = nullptr;
		const JSONNode &recipients = p["recipients"];
		for (auto it2 = recipients.begin(); it2 != recipients.end(); ++it2)
			pUser = PrepareUser(*it2);

		if (pUser == nullptr)
			continue;
		
		CMStringW wszChannelId = p["id"].as_mstring();
		pUser->channelId = _wtoi64(wszChannelId);
		pUser->lastMsgId = ::getId(p["last_message_id"]);
		pUser->bIsPrivate = true;

		setId(pUser->hContact, DB_KEY_CHANNELID, pUser->channelId);

		SnowFlake oldMsgId = getId(pUser->hContact, DB_KEY_LASTMSGID);
		if (pUser->lastMsgId > oldMsgId)
			RetrieveHistory(pUser->hContact, MSG_AFTER, oldMsgId, 99);
	}

	const JSONNode &readState = pRoot["read_state"];
	for (auto it = readState.begin(); it != readState.end(); ++it) {
		const JSONNode &p = *it;
		CDiscordUser *pUser = FindUserByChannel(::getId(p["id"]));
		if (pUser != nullptr)
			pUser->lastReadId = ::getId(p["last_message_id"]);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////
// UTN support

void CDiscordProto::OnCommandTyping(const JSONNode &pRoot)
{
	SnowFlake userId = ::getId(pRoot["user_id"]);
	SnowFlake channelId = ::getId(pRoot["channel_id"]);
	debugLogA("user typing notification: userid=%lld, channelid=%lld", userId, channelId);

	CDiscordUser *pUser = FindUser(userId);
	if (pUser == nullptr) {
		debugLogA("user with id=%lld is not found", userId);
		return;
	}

	if (pUser->channelId == channelId) {
		debugLogA("user is typing in his private channel");
		CallService(MS_PROTO_CONTACTISTYPING, pUser->hContact, 20);
	}
	else {
		debugLogA("user is typing in a group channel, skipped");
	}
}

/////////////////////////////////////////////////////////////////////////////////////////
// User info update

void CDiscordProto::OnCommandUserUpdate(const JSONNode &pRoot)
{
	SnowFlake id = ::getId(pRoot["id"]);

	MCONTACT hContact;
	if (id != m_ownId) {
		CDiscordUser *pUser = FindUser(id);
		if (pUser == nullptr)
			return;

		hContact = pUser->hContact;
	}
	else hContact = 0;

	// force rereading avatar
	CheckAvatarChange(hContact, pRoot["avatar"].as_mstring());
}

void CDiscordProto::OnCommandUserSettingsUpdate(const JSONNode &pRoot)
{
	int iStatus = StrToStatus(pRoot["status"].as_mstring());
	if (iStatus != 0) {
		int iOldStatus = m_iStatus; m_iStatus = iStatus;
		ProtoBroadcastAck(0, ACKTYPE_STATUS, ACKRESULT_SUCCESS, (HANDLE)iOldStatus, m_iStatus);
	}
}
