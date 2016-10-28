/*
 *	PROGRAM:	SQL Trace plugin
 *	MODULE:		traceplugin.cpp
 *	DESCRIPTION:	Exported entrypoints for the plugin
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
 *  The Original Code was created by Nickolay Samofatov
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2004 Nickolay Samofatov <nickolay@broadviewsoftware.com>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 *
 *  2008 Khorsun Vladyslav
 */

#include "firebird.h"
#include "../jrd/SimilarToMatcher.h"
#include "../common/classes/ImplementHelper.h"
#include "../jrd/trace/TraceSession.h"

#include "TraceConfiguration.h"
#include "TracePluginImpl.h"


class TraceFactoryImpl FB_FINAL :
	public Firebird::StdPlugin<Firebird::ITraceFactoryImpl<TraceFactoryImpl, Firebird::CheckStatusWrapper> >
{
public:
	explicit TraceFactoryImpl(Firebird::IPluginConfig*)
	{ }

	// TraceFactory implementation
	ntrace_mask_t trace_needs();
	Firebird::ITracePlugin* trace_create(Firebird::CheckStatusWrapper* status,
		Firebird::ITraceInitInfo* init_info);
	int release();
};

int TraceFactoryImpl::release()
{
	if (--refCounter == 0)
	{
		delete this;
		return 0;
	}
	return 1;
}

ntrace_mask_t TraceFactoryImpl::trace_needs()
{
	return (1 << Firebird::ITraceFactory::TRACE_EVENT_MAX) - 1;
}

Firebird::ITracePlugin* TraceFactoryImpl::trace_create(Firebird::CheckStatusWrapper* status,
	Firebird::ITraceInitInfo* initInfo)
{
	Firebird::MasterInterfacePtr master;
	const char* dbname = NULL;
	try
	{
		dbname = initInfo->getDatabaseName();
		if (!dbname)
			dbname = "";

		TracePluginConfig config;
		TraceCfgReader::readTraceConfiguration(initInfo->getConfigText(), dbname, config);

		Firebird::ITraceDatabaseConnection* connection = initInfo->getConnection();
		Firebird::ITraceServiceConnection* service = initInfo->getService();

		if (!config.enabled ||
			(config.connection_id && connection &&
				(connection->getConnectionID() != config.connection_id)))
		{
			return NULL; // Plugin is not needed, no error happened.
		}

		Firebird::string& inc_user = config.include_user_filter;
		Firebird::string& exc_user = config.exclude_user_filter;
		Firebird::string& inc_process = config.include_process_filter;
		Firebird::string& exc_process = config.exclude_process_filter;

		const char* user = NULL;
		const char* process = NULL;
		if (connection) {
			user = connection->getUserName();
			process = connection->getRemoteProcessName();
		} 
		else if (service) {
			user = service->getUserName();
			process = service->getRemoteProcessName();
		}

		if (!user && inc_user.hasData() || !process && inc_process.hasData())
		{
			// Unknown user or remote process but we want specific
			// Don't start trace session
			return NULL; 
		}

		bool enabled = true;
		if ((inc_user.hasData() || exc_user.hasData()) && user ||
			(inc_process.hasData() || exc_process.hasData()) && process)
		{
			typedef Jrd::UpcaseConverter<Jrd::NullStrConverter>	SimilarConverter;

			UnicodeCollationHolder unicodeCollation(*getDefaultMemoryPool());
			Jrd::TextType *textType = unicodeCollation.getTextType();

			if (inc_user.hasData())
			{
				Firebird::SimilarToMatcher<UCHAR, SimilarConverter> include_matcher (
					*getDefaultMemoryPool(), textType, (const UCHAR*) inc_user.c_str(),
					inc_user.length(), '\\', true);

				include_matcher.process((const UCHAR*) user, strlen(user));
				enabled = include_matcher.result();
			}

			if (enabled && exc_user.hasData())
			{
				Firebird::SimilarToMatcher<UCHAR, SimilarConverter> exclude_matcher (
					*getDefaultMemoryPool(), textType, (const UCHAR*) exc_user.c_str(),
					exc_user.length(), '\\', true);

				exclude_matcher.process((const UCHAR*) user, strlen(user));
				enabled = !exclude_matcher.result();
			}

			if (enabled && inc_process.hasData())
			{
				Firebird::SimilarToMatcher<UCHAR, SimilarConverter> include_matcher (
					*getDefaultMemoryPool(), textType, (const UCHAR*) inc_process.c_str(),
					inc_process.length(), '\\', true);

				include_matcher.process((const UCHAR*) process, strlen(process));
				enabled = include_matcher.result();
			}

			if (enabled && exc_process.hasData())
			{
				Firebird::SimilarToMatcher<UCHAR, SimilarConverter> exclude_matcher (
					*getDefaultMemoryPool(), textType, (const UCHAR*) exc_process.c_str(),
					exc_process.length(), '\\', true);

				exclude_matcher.process((const UCHAR*) process, strlen(process));
				enabled = !exclude_matcher.result();
			}
		}

		if (!enabled)
		{
			// User filter not matched - does not start trace session
			return NULL;
		}

		if (config.format != 0 && !(initInfo->getTraceSessionFlags() & Firebird::trs_system))
			Firebird::fatal_exception::raiseFmt("User sessions can't use trace format = %d", config.format);

		Firebird::AutoPtr<Firebird::ITraceLogWriter, Firebird::SimpleRelease<Firebird::ITraceLogWriter> >
			logWriter(initInfo->getLogWriter());

		if (logWriter)
			config.log_filename = "";

		return FB_NEW TracePluginImpl(this, config, initInfo);	// Everything is ok, we created a plugin

	}
	catch (Firebird::Exception& ex)
	{
		// put error into trace log
		Firebird::ITraceLogWriter* logWriter = initInfo->getLogWriter();
		if (logWriter)
		{
			const char* strEx = TracePluginImpl::marshal_exception(ex);
			Firebird::string err;
			if (dbname)
				err.printf("Error creating trace session for database \"%s\":\n%s\n", dbname, strEx);
			else
				err.printf("Error creating trace session for service manager attachment:\n%s\n", strEx);

			logWriter->write(err.c_str(), err.length());
			logWriter->release();
		}
		else
			ex.stuffException(status);
	}

	return NULL;
}


static Firebird::SimpleFactory<TraceFactoryImpl> traceFactory;

void registerTrace(Firebird::IPluginManager* iPlugin)
{
	iPlugin->registerPluginFactory(Firebird::IPluginManager::TYPE_TRACE, "fbtrace", &traceFactory);
	Firebird::getUnloadDetector()->registerMe();
}


extern "C" void FB_EXPORTED FB_PLUGIN_ENTRY_POINT(Firebird::IMaster* master)
{
	Firebird::CachedMasterInterface::set(master);
	registerTrace(Firebird::PluginManagerInterfacePtr());
}
