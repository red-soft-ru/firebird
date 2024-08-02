/*
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
 *  The Original Code was created by Adriano dos Santos Fernandes
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2024 Adriano dos Santos Fernandes <adrianosf@gmail.com>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#ifndef JRD_SYSTEM_TRIGGERS_H
#define JRD_SYSTEM_TRIGGERS_H

#include "firebird.h"

namespace Jrd
{
	class thread_db;
	class Record;
}	// namespace Jrd

namespace Jrd::SystemTriggers
{
	void beforeDeleteCheckConstraint(thread_db* tdbb, Record* record);
	void afterDeleteCheckConstraint(thread_db* tdbb, Record* record);
	void beforeUpdateCheckConstraint(thread_db* tdbb, Record* orgRecord, Record* newRecord);
	void beforeDeleteIndex(thread_db* tdbb, Record* record);
	void beforeUpdateIndex(thread_db* tdbb, Record* orgRecord, Record* newRecord);
	void beforeDeleteIndexSegment(thread_db* tdbb, Record* record);
	void beforeUpdateIndexSegment(thread_db* tdbb, Record* orgRecord, Record* newRecord);
	void beforeUpdateField(thread_db* tdbb, Record* orgRecord, Record* newRecord);
	void beforeInsertRefConstraint(thread_db* tdbb, Record* record);
	void beforeDeleteRelationConstraint(thread_db* tdbb, Record* record);
	void afterDeleteRelationConstraint(thread_db* tdbb, Record* record);
	void beforeInsertRelationConstraint(thread_db* tdbb, Record* record);
	void beforeDeleteRelationField(thread_db* tdbb, Record* record);
	void afterDeleteRelationField(thread_db* tdbb, Record* record);
	void beforeUpdateRelationField(thread_db* tdbb, Record* orgRecord, Record* newRecord);
	void beforeDeleteTrigger(thread_db* tdbb, Record* record);
	void beforeUpdateTrigger(thread_db* tdbb, Record* orgRecord, Record* newRecord);
	void beforeDeleteUserPrivilege(thread_db* tdbb, Record* record);
	void beforeInsertUserPrivilege(thread_db* tdbb, Record* record);
}	// namespace Jrd::SystemTriggers

#endif	// JRD_SYSTEM_TRIGGERS_H
