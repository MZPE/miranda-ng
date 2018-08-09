#include "stdafx.h"

bool CToxProto::IsOnline()
{
	return m_toxThread && m_iStatus >= ID_STATUS_ONLINE;
}

void CToxProto::TryConnect(Tox *tox)
{
	TOX_CONNECTION connectionStatus = tox_self_get_connection_status(tox);
	if (connectionStatus != TOX_CONNECTION_NONE) {
		debugLogA(__FUNCTION__": successfuly connected to DHT");

		m_iStatus = m_iDesiredStatus;
		ProtoBroadcastAck(NULL, ACKTYPE_STATUS, ACKRESULT_SUCCESS, (HANDLE)ID_STATUS_CONNECTING, m_iStatus);
		tox_self_set_status(tox, MirandaToToxStatus(m_iStatus));

		debugLogA(__FUNCTION__": changing status from %i to %i", ID_STATUS_CONNECTING, m_iDesiredStatus);

		UpdateStatusMenu(NULL, NULL);

		LoadFriendList(tox);
		return;
	}

	int maxConnectRetries = getByte("MaxConnectRetries", TOX_MAX_CONNECT_RETRIES);
	if (m_iStatus++ > maxConnectRetries) {
		SetStatus(ID_STATUS_OFFLINE);
		ProtoBroadcastAck(NULL, ACKTYPE_LOGIN, ACKRESULT_FAILED, nullptr, LOGINERR_NONETWORK);
		debugLogA(__FUNCTION__": failed to connect to DHT");
		return;
	}
}

void CToxProto::CheckConnection(Tox *tox, int &retriesCount)
{
	int maxReconnectRetries = getByte("MaxReconnectRetries", TOX_MAX_RECONNECT_RETRIES);
	TOX_CONNECTION connectionStatus = tox_self_get_connection_status(tox);
	if (connectionStatus != TOX_CONNECTION_NONE) {
		if (retriesCount < maxReconnectRetries) {
			debugLogA(__FUNCTION__": restored connection with DHT");
			retriesCount = maxReconnectRetries;
		}
	}
	else {
		if (retriesCount == maxReconnectRetries) {
			retriesCount--;
			debugLogA(__FUNCTION__": lost connection with DHT");
		}
		else if (!(--retriesCount)) {
			debugLogA(__FUNCTION__": disconnected from DHT");
			SetStatus(ID_STATUS_OFFLINE);
			return;
		}
	}
}

void CToxProto::CheckingThread(void *arg)
{
	Thread_SetName(MODULE ": CheckingThread");

	debugLogA(__FUNCTION__": entering");

	CToxThread *thread = (CToxThread*)arg;
	int retriesCount = getByte("MaxReconnectRetries", TOX_MAX_RECONNECT_RETRIES);
	while (!thread->IsTerminated()) {
		if (m_iStatus < ID_STATUS_ONLINE)
			TryConnect(thread->Tox());
		else
			CheckConnection(thread->Tox(), retriesCount);

		WaitForSingleObject(hTerminateEvent, TOX_CHECKING_INTERVAL);
	}

	hCheckingThread = nullptr;

	debugLogA(__FUNCTION__": leaving");
}

void CToxProto::PollingThread(void*)
{
	Thread_SetName(MODULE ": PollingThread");

	debugLogA(__FUNCTION__": entering");

	Tox_Options *options = GetToxOptions();
	if (!options) {
		SetStatus(ID_STATUS_OFFLINE);
		ProtoBroadcastAck(NULL, ACKTYPE_LOGIN, ACKRESULT_FAILED, nullptr);
		debugLogA(__FUNCTION__": leaving");
		return;
	}

	TOX_ERR_NEW error;
	CToxThread toxThread(options, &error);
	if (error != TOX_ERR_NEW_OK) {
		SetStatus(ID_STATUS_OFFLINE);
		debugLogA(__FUNCTION__": failed to initialize tox core (%d)", error);
		ShowNotification(TranslateT("Unable to initialize Tox core"), ToxErrorToString(error), MB_ICONERROR);
		tox_options_free(options);
		debugLogA(__FUNCTION__": leaving");
		return;
	}
	tox_options_free(options);

	m_toxThread = &toxThread;
	InitToxCore(toxThread.Tox());
	BootstrapNodes(toxThread.Tox());
	hCheckingThread = ForkThreadEx(&CToxProto::CheckingThread, &toxThread, nullptr);

	while (!toxThread.IsTerminated()) {
		tox_iterate(toxThread.Tox(), this);
		uint32_t interval = tox_iteration_interval(toxThread.Tox());
		interval = interval 
			? interval
			: TOX_DEFAULT_INTERVAL;
		WaitForSingleObject(hTerminateEvent, interval);
	}

	SetEvent(hTerminateEvent);
	WaitForSingleObject(hCheckingThread, TOX_CHECKING_INTERVAL);

	UninitToxCore(toxThread.Tox());
	m_toxThread = nullptr;
	hPollingThread = nullptr;

	debugLogA(__FUNCTION__": leaving");
}