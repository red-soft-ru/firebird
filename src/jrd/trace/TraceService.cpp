/*
 *	MODULE:		TraceService.cpp
 *	DESCRIPTION:
 *
 *  The contents of this file are subject to the Initial
 *  Developer's Public License Version 1.0 (the "License");
 *  you may not use this file except in compliance with the
 *  License. You may obtain a copy of the License at
 *  http://www.ibphoenix.com/main.nfs?a=ibphoenix&page=ibp_idpl.
 *
 *  Software distributed under the License is distributed AS IS,
 *  WITHOUT WARRANTY OF ANY KIND, either express or implied.
 *  See the License for the specific language governing rights
 *  and limitations under the License.
 *
 *  The Original Code was created by Khorsun Vladyslav
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2008 Khorsun Vladyslav <hvlad@users.sourceforge.net>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 *
 */

#include "firebird.h"
#include "consts_pub.h"
#include "fb_exception.h"
#include "iberror.h"
#include "../../common/classes/fb_string.h"
#include "../../common/classes/timestamp.h"
#include "../../common/config/config.h"
#include "../../common/StatusArg.h"
#include "../../common/ThreadStart.h"
#include "../../jrd/svc.h"
#include "../../common/os/guid.h"
#include "../../jrd/trace/TraceLog.h"
#include "../../jrd/trace/TraceManager.h"
#include "../../jrd/trace/TraceService.h"
#include "../../jrd/Mapping.h"

using namespace Firebird;
using namespace Jrd;


class TraceSvcJrd : public TraceSvcIntf
{
public:
	explicit TraceSvcJrd(Service& svc) :
		m_svc(svc),
		m_admin(false),
		m_chg_number(0)
	{};

	virtual ~TraceSvcJrd() {};

	virtual void setAttachInfo(const string& service_name, const string& user, const string& pwd,
		bool trusted);

	virtual void startSession(TraceSession& session, bool interactive);
	virtual void stopSession(ULONG id);
	virtual void setActive(ULONG id, bool active);
	virtual void listSessions();

private:
	void readSession(TraceSession& session);
	bool changeFlags(ULONG id, int setFlags, int clearFlags);
	bool checkAliveAndFlags(ULONG sesId, int& flags);

	// Check if current service is allowed to list\stop given other session
	bool checkPrivileges(TraceSession& session);

	Service& m_svc;
	string m_user;
	AuthReader::AuthBlock m_authBlock;
	bool   m_admin;
	ULONG  m_chg_number;
};

void TraceSvcJrd::setAttachInfo(const string& /*svc_name*/, const string& user, const string& /*pwd*/,
	bool /*trusted*/)
{
	const unsigned char* bytes;
	unsigned int authBlockSize = m_svc.getAuthBlock(&bytes);
	if (authBlockSize)
	{
		m_authBlock.add(bytes, authBlockSize);
		m_user = "";
	}
	else
	{
		m_user = user;
		m_admin = (m_user == SYSDBA_USER_NAME);
	}
}

void TraceSvcJrd::startSession(TraceSession& session, bool interactive)
{
	if (!TraceManager::pluginsCount())
	{
		m_svc.printf(false, "Can not start trace session. There are no trace plugins loaded\n");
		return;
	}

	ConfigStorage* storage = TraceManager::getStorage();

	{	// scope
		StorageGuard guard(storage);

		session.ses_auth = m_authBlock;
		session.ses_user = m_user.hasData() ? m_user : m_svc.getUserName();

		session.ses_flags = trs_active;
		if (m_admin) {
			session.ses_flags |= trs_admin;
		}

		if (interactive)
		{
			Guid guid;
			GenerateGuid(&guid);

			char* buff = session.ses_logfile.getBuffer(GUID_BUFF_SIZE);
			GuidToString(buff, &guid);

			session.ses_logfile.insert(0, "fb_trace.");
		}

		storage->addSession(session);
		m_chg_number = storage->getChangeNumber();
	}

	m_svc.started();
	m_svc.printf(false, "Trace session ID %ld started\n", session.ses_id);

	if (interactive)
	{
		readSession(session);
		{
			StorageGuard guard(storage);
			storage->removeSession(session.ses_id);
		}
	}
}

void TraceSvcJrd::stopSession(ULONG id)
{
	m_svc.started();

	ConfigStorage* storage = TraceManager::getStorage();
	StorageGuard guard(storage);

	storage->restart();

	TraceSession session(*getDefaultMemoryPool());
	while (storage->getNextSession(session))
	{
		if (id != session.ses_id)
			continue;

		if (checkPrivileges(session))
		{
			storage->removeSession(id);
			m_svc.printf(false, "Trace session ID %ld stopped\n", id);
		}
		else
			m_svc.printf(false, "No permissions to stop other user trace session\n");

		return;
	}

	m_svc.printf(false, "Trace session ID %d not found\n", id);
}

void TraceSvcJrd::setActive(ULONG id, bool active)
{
	if (active)
	{
		if (changeFlags(id, trs_active, 0)) {
			m_svc.printf(false, "Trace session ID %ld resumed\n", id);
		}
	}
	else
	{
		if (changeFlags(id, 0, trs_active)) {
			m_svc.printf(false, "Trace session ID %ld paused\n", id);
		}
	}
}

bool TraceSvcJrd::changeFlags(ULONG id, int setFlags, int clearFlags)
{
	ConfigStorage* storage = TraceManager::getStorage();
	StorageGuard guard(storage);

	storage->restart();

	TraceSession session(*getDefaultMemoryPool());
	while (storage->getNextSession(session))
	{
		if (id != session.ses_id)
			continue;

		if (checkPrivileges(session))
		{
			const int saveFlags = session.ses_flags;

			session.ses_flags |= setFlags;
			session.ses_flags &= ~clearFlags;

			if (saveFlags != session.ses_flags) {
				storage->updateSession(session);
			}

			return true;
		}

		m_svc.printf(false, "No permissions to change other user trace session\n");
		return false;
	}

	m_svc.printf(false, "Trace session ID %d not found\n", id);
	return false;
}

void TraceSvcJrd::listSessions()
{
	m_svc.started();

	// Don't put data into service while storage is locked, see gh-8151

	typedef HalfStaticArray<char, 1024> CharBufferBase;

	class CharBuff : public CharBufferBase
	{
	public:
		CharBuff() : CharBufferBase(*getDefaultMemoryPool())
		{}

		// Format string and append it to the end of buffer
		void printf(const char* format, ...)
		{
			string str;

			va_list params;
			va_start(params, format);
			str.vprintf(format, params);
			va_end(params);

			this->append(str.c_str(), str.length());
		}

	};

	CharBuff buff;

	ConfigStorage* storage = TraceManager::getStorage();
	{
		StorageGuard guard(storage);

		storage->restart();

		TraceSession session(*getDefaultMemoryPool());
		while (storage->getNextSession(session))
		{
			if (checkPrivileges(session))
			{
				buff.printf("\nSession ID: %d\n", session.ses_id);
				if (!session.ses_name.empty()) {
					buff.printf("  name:  %s\n", session.ses_name.c_str());
				}
				buff.printf("  user:  %s\n", session.ses_user.c_str());

				struct tm* t = localtime(&session.ses_start);
				buff.printf("  date:  %04d-%02d-%02d %02d:%02d:%02d\n",
					t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
					t->tm_hour, t->tm_min, t->tm_sec);

				string flags;
				if (session.ses_flags & trs_active) {
					flags = "active";
				}
				else {
					flags = "suspend";
				}
				if (session.ses_flags & trs_admin) {
					flags += ", admin";
				}
				if (session.ses_flags & trs_system) {
					flags += ", system";
				}
				if (session.ses_logfile.empty()) {
					flags += ", audit";
				}
				else {
					flags += ", trace";
				}
				if (session.ses_flags & trs_log_full) {
					flags += ", log full";
				}
				buff.printf("  flags: %s\n", flags.c_str());
			}
		}
	}

	if (buff.hasData())
		m_svc.putBytes(reinterpret_cast<UCHAR*>(buff.begin()), buff.getCount());
}

void TraceSvcJrd::readSession(TraceSession& session)
{
	const size_t maxLogSize = Config::getMaxUserTraceLogSize(); // in MB

	if (session.ses_logfile.empty())
	{
		m_svc.printf(false, "Can't open trace data log file");
		return;
	}

	MemoryPool& pool = *getDefaultMemoryPool();
	AutoPtr<TraceLog> log(FB_NEW_POOL(pool) TraceLog(pool, session.ses_logfile, true));

	UCHAR buff[1024];
	int flags = session.ses_flags;
	while (!m_svc.finished() && checkAliveAndFlags(session.ses_id, flags))
	{
		const FB_SIZE_T len = log->read(buff, sizeof(buff));
		if (!len)
		{
			if (!checkAliveAndFlags(session.ses_id, flags))
				break;

			if (m_svc.svc_detach_sem.tryEnter(0, 250))
				break;
		}
		else
		{
			m_svc.putBytes(buff, len);

			const bool logFull = (flags & trs_log_full);
			if (logFull && log->getApproxLogSize() <= maxLogSize)
			{
				// resume session
				changeFlags(session.ses_id, 0, trs_log_full);
			}
		}
	}
}

bool TraceSvcJrd::checkAliveAndFlags(ULONG sesId, int& flags)
{
	ConfigStorage* storage = TraceManager::getStorage();

	bool alive = (m_chg_number == storage->getChangeNumber());
	if (!alive)
	{
		// look if our session still alive
		StorageGuard guard(storage);

		TraceSession readSession(*getDefaultMemoryPool());
		storage->restart();
		while (storage->getNextSession(readSession))
		{
			if (readSession.ses_id == sesId)
			{
				alive = true;
				flags = readSession.ses_flags;
				break;
			}
		}

		m_chg_number = storage->getChangeNumber();
	}

	return alive;
}

bool TraceSvcJrd::checkPrivileges(TraceSession& session)
{
	// Our service run in embedded mode and have no auth info - trust user name as is
	if (m_admin || m_user.hasData() && (m_user == session.ses_user))
		return true;

	// Other session is fully authorized - try to map our auth info using other's 
	// security database
	if (session.ses_auth.hasData())
	{
		AuthReader::Info info;
		for (AuthReader rdr(session.ses_auth); rdr.getInfo(info); rdr.moveNext())
		{
			const char* secDb = info.secDb.hasData() ? info.secDb.c_str() :
				Config::getDefaultConfig()->getSecurityDatabase();
			string s_user, t_role;

			try
			{
				mapUser(s_user, t_role, NULL, NULL, m_authBlock, "services manager", 
					NULL, secDb, m_svc.getCryptCallback(), NULL, false);
			}
			catch (const Firebird::Exception&)
			{
				// Error in mapUser() means missing context, therefore...
				continue;
			}

			t_role.upper();
			if (s_user == SYSDBA_USER_NAME || t_role == ADMIN_ROLE || s_user == session.ses_user)
				return true;
		}
	}
	// Other session's service run in embedded mode - check our user name as is
	else if (m_svc.getUserName() == session.ses_user || m_svc.getUserAdminFlag())
		return true;

	return false;
}

// service entrypoint
int TRACE_main(UtilSvc* arg)
{
	Service* svc = (Service*) arg;
	int exit_code = FB_SUCCESS;

	TraceSvcJrd traceSvc(*svc);
	try
	{
		fbtrace(svc, &traceSvc);
	}
	catch (const Exception& e)
	{
		StaticStatusVector status;
		e.stuffException(status);
		svc->initStatus();
		svc->setServiceStatus(status.begin());
		exit_code = FB_FAILURE;
	}

	return exit_code;
}
